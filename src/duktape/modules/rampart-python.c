/* Copyright (C) 2023  Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#define PY_SSIZE_T_CLEAN

#ifdef _XOPEN_SOURCE
#define _XOPEN_SOURCE_BAK _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif

#include "Python.h"

#ifdef _XOPEN_SOURCE_BAK
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE _XOPEN_SOURCE_BAK
#undef _XOPEN_SOURCE_BAK
#endif

#include "marshal.h"
#include "datetime.h"
#include <time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include "event.h"
#include "rampart.h"

static int python_is_init=0;
static int is_child=0;

#define dprintf2(...) do{\
    fprintf(stderr, "(%d) at %d (thread %d): ", (int)getpid(),__LINE__, get_thread_num());\
    fprintf(stderr, __VA_ARGS__);\
    fflush(stderr);\
}while(0)

//#define rpydebug 1

#ifdef rpydebug

#define dprintf(...) do{\
    printf("(%d) at %d (thread %d): ", (int)getpid(),__LINE__, get_thread_num());\
    printf(__VA_ARGS__);\
    fflush(stdout);\
}while(0)

#define dprintfhex(_h,_hsz,...) do{\
    printf("(%d) at %d (thread %d): ", (int)getpid(),__LINE__, get_thread_num());\
    printf(__VA_ARGS__);\
    printhex(_h,_hsz);putchar('\n');\
    fflush(stdout);\
}while(0)


#define printhex(sbuf,sz) do{\
    unsigned char *buf=(unsigned char*)(sbuf), *end;\
    char *out=NULL,*p;\
    /*int ucase=7;*/\
    int ucase=39;\
    end=buf+(sz);\
    REMALLOC(out,(sz)*2+1);\
    p=out;\
    while(buf<end)\
    {\
        int nibval;\
        nibval=*buf/16 + 48;\
        if(nibval>57) nibval+=ucase;\
        *p=nibval;\
        p++;\
        nibval=*buf%16 +48;\
        if(nibval>57) nibval+=ucase;\
        *p=nibval;\
        p++;\
        buf++;\
    }\
    *p='\0';\
    printf("%s",out);\
    free(out);\
}while(0)

#define RP_Py_XDECREF(r) do{\
    if(r){\
        int cnt=(int)Py_REFCNT((r));\
        dprintf("xdecref, refcnt was %d\n",cnt);\
        /* deallocated objects often return a random negative number*/\
        if(cnt<0) dprintf("DANGER WILL ROBINSON!!! possible xdecref on deallocated python object\n");\
        if(cnt>0) Py_XDECREF((r));\
        if(!cnt) dprintf("DANGER, xdecref on an item with refcnt=0\n");\
        r=NULL;\
    }\
    else  dprintf("xdecref, ref was null\n");\
} while(0)

#else

#define dprintf(...) /* nada */

#define dprintfhex(...) /* nada */

#define RP_Py_XDECREF(r) do{\
    if(r) {Py_XDECREF((r));r=NULL;}\
} while(0)

#endif



#define PYLOCK ({\
    PyGILState_STATE _s={0};\
    if(!is_child) {\
        dprintf("GETTING pylock\n");\
        /*if (!PyGILState_Check()) dprintf("is already locked\n");*/\
        _s=PyGILState_Ensure();\
        dprintf("GOT pylock\n");\
    }\
    _s;\
})

#define PYUNLOCK(s) do {\
    if(!is_child) {\
        PyGILState_Release((s));\
        dprintf("RELEASED pylock\n");\
    }\
}while (0)

pthread_mutex_t rpy_lock;
RPTHR_LOCK *rp_rpy_lock;
#define RPYLOCK RP_MLOCK(rp_rpy_lock)
#define RPYUNLOCK RP_MUNLOCK(rp_rpy_lock)

#define MAX_EXCEPTION_LENGTH 4095

static char *get_exception(char *buf)
{
    PyObject *ptype, *pvalue, *ptraceback;
    const char* err_msg="\nunknown error";
    int len=0, left=MAX_EXCEPTION_LENGTH;
    char *s=buf;

    PyErr_Fetch(&ptype, &pvalue, &ptraceback);  //this does a pyerr_clear()
    PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);
    if(pvalue) {
        PyObject *pstr = PyObject_Str(pvalue);
        if(pstr)
        {
            err_msg = PyUnicode_AsUTF8(pstr);
            len = snprintf(s,left,"\n%s", err_msg);
            left-=len;
            s+=len;
            
            if (ptraceback && PyTraceBack_Check(ptraceback))
            {
                PyTracebackObject* traceRoot = (PyTracebackObject*)ptraceback;
                PyTracebackObject* pTrace = traceRoot;
                int i=1;
                
                while (pTrace != NULL)
                {
                    PyFrameObject* frame = pTrace->tb_frame;
                    PyCodeObject* code = PyFrame_GetCode(frame);

                    int lineNr = PyFrame_GetLineNumber(frame);
                    const char *sCodeName = PyUnicode_AsUTF8(code->co_name);
                    const char *sFileName = PyUnicode_AsUTF8(code->co_filename);

                    len = snprintf(s, left, "\n    at python:%s (%s:%d)", sCodeName, sFileName, lineNr);
                    left-=len;
                    s+=len;
                    i++;
                    if(len<0)
                        break;
                    pTrace = pTrace->tb_next;
                }       
            }
        }
        else
            sprintf(buf, err_msg);
    }
    else
        *buf='\0';

    RP_Py_XDECREF(ptype);
    RP_Py_XDECREF(pvalue);
    RP_Py_XDECREF(ptraceback);
    PyErr_Clear();//unnecessary?

    return buf;
}


/* init python */


static void init_python(const char *program_name, char *ppath)
{

    Py_Initialize();
    PyGILState_STATE state=PYLOCK;
    PyObject *paths = PyTuple_New((Py_ssize_t) 4);
    char tpath[PATH_MAX+1];

    PyTuple_SetItem(paths, (Py_ssize_t) 0, PyUnicode_FromString( "./"  ));
    PyTuple_SetItem(paths, (Py_ssize_t) 1, PyUnicode_FromString( ppath  ));
    snprintf(tpath, PATH_MAX, "%s/site-packages", ppath);
    PyTuple_SetItem(paths, (Py_ssize_t) 2, PyUnicode_FromString( tpath  ));
    snprintf(tpath, PATH_MAX, "%s/lib-dynload", ppath);
    PyTuple_SetItem(paths, (Py_ssize_t) 3, PyUnicode_FromString( tpath  ));

    PySys_SetObject("path", paths);
    PYUNLOCK(state);

    if(!is_child)
        (void)PyEval_SaveThread(); //so badly named, when what we want is to begin in an unlocked state.
}

char ppath[PATH_MAX-15];  //room for /site-packages\0

static duk_ret_t rp_duk_python_init(duk_context *ctx)
{
    RPYLOCK;
    if(!python_is_init)
    {
        // default is modules_dir/python3-lib, leave space for subdirs as well
        if ( strlen(modules_dir) + 27 > PATH_MAX)
            RP_THROW(ctx, "python.init(): Total path length of '%s/python3-lib' is too long", modules_dir);
        strcpy(ppath,modules_dir);
        strcat(ppath,"/python3-lib");

        init_python(rampart_exec, ppath);
        python_is_init=1;

    }
    RPYUNLOCK;
    return 0;
}

/* ****************** JSVAR -> PYTHON VAR ******************* */

static PyObject * type_to_pytype(duk_context *ctx, duk_idx_t idx);

static PyObject * bool_to_pybool(duk_context *ctx, duk_idx_t idx)
{
    long in =(long) duk_get_boolean(ctx, idx);
    
    return PyBool_FromLong(in);
}

static PyObject * num_to_pyfloat(duk_context *ctx, duk_idx_t idx)
{
    double in = (double)duk_get_number(ctx, idx);
    
    return PyFloat_FromDouble(in);
}

static PyObject * num_to_pyint(duk_context *ctx, duk_idx_t idx)
{
    long in = (long) duk_get_int(ctx, idx);
    
    return PyLong_FromLong(in);
}

static PyObject * str_to_pyint(duk_context *ctx, duk_idx_t idx, int base)
{
    const char *in = duk_get_string(ctx, idx);
    
    return PyLong_FromString(in, NULL, base);
}

static PyObject * str_to_pystr(duk_context *ctx, duk_idx_t idx)
{
    const char *in = duk_get_string(ctx, idx);
    
    return PyUnicode_FromString(in);
}



static PyObject * array_to_pycomplex(duk_context *ctx, duk_idx_t idx)
{
    double r,j;

    duk_get_prop_index(ctx, idx, 0);
    r = duk_get_number_default(ctx, -1, 0);
    duk_pop(ctx);

    duk_get_prop_index(ctx, idx, 1);
    j = duk_get_number_default(ctx, -1, 0);
    duk_pop(ctx);

    return PyComplex_FromDoubles(r,j);
}

static PyObject * buf_to_pybytes(duk_context *ctx, duk_idx_t idx)
{
    duk_size_t sz;
    const char * in = (const char *) duk_get_buffer_data(ctx, idx, &sz);

    if(!in)
        return NULL;

    return PyBytes_FromStringAndSize(in, (Py_ssize_t)sz);
}

static PyObject * buf_to_pybytearray(duk_context *ctx, duk_idx_t idx)
{
    duk_size_t sz;
    const char * in = (const char *) duk_get_buffer_data(ctx, idx, &sz);

    if(!in)
        return NULL;

    return PyByteArray_FromStringAndSize(in, (Py_ssize_t)sz);
}

static PyObject *toPy_check_ref(duk_context *ctx, duk_idx_t idx)
{
    PyObject *ret=NULL;
    char ptr_str[32];
    void *p = duk_get_heapptr(ctx, idx);

    snprintf(ptr_str,32,"%p", p); //the key to store/retrieve the PyObject pointer
    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("jstopymap"));

    if(duk_get_prop_string(ctx, -1, ptr_str))
    {
        ret=(PyObject *)duk_get_pointer(ctx, -1);
    }

    duk_pop_2(ctx);

    return ret;
}

static void toPy_store_ref(duk_context *ctx, duk_idx_t idx, PyObject *val)
{
    char ptr_str[32];
    void *p = duk_get_heapptr(ctx, idx);

    snprintf(ptr_str,32,"%p", p); //the key to store/retrieve the PyObject pointer

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("jstopymap"));

    duk_push_pointer(ctx, (void*) val);
    duk_put_prop_string(ctx, -2, ptr_str);
    duk_pop(ctx);
}

static PyObject * array_to_pylist(duk_context *ctx, duk_idx_t idx)
{
    duk_size_t i=0, n = duk_get_length(ctx, idx);
    PyObject *ret = NULL;

    ret=toPy_check_ref(ctx, idx);

    if(ret)
        return ret;

    ret = PyList_New((Py_ssize_t) n);

    toPy_store_ref(ctx, idx, ret);

    while (i < n)
    {
        duk_get_prop_index(ctx, idx, (duk_uarridx_t)i);
        PyList_SetItem(ret, (Py_ssize_t) i, type_to_pytype(ctx, -1) );
        duk_pop(ctx);
        i++;
    }
    return ret;
}

static PyObject * array_to_pytuple(duk_context *ctx, duk_idx_t idx)
{
    duk_size_t i=0, n = duk_get_length(ctx, idx);
    PyObject *ret = NULL;

    ret=toPy_check_ref(ctx, idx);

    if(ret)
        return ret;

    ret = PyTuple_New((Py_ssize_t) n);

    toPy_store_ref(ctx, idx, ret);


    while (i < n)
    {
        duk_get_prop_index(ctx, idx, (duk_uarridx_t)i);
        PyTuple_SetItem(ret, (Py_ssize_t) i, type_to_pytype(ctx, -1) );
        duk_pop(ctx);
        i++;
    }
    return ret;
}

static PyObject * obj_to_pydict(duk_context *ctx, duk_idx_t idx)
{
    PyObject *val=NULL, *ret = NULL;
    const char *key=NULL;

    ret=toPy_check_ref(ctx, idx);

    if(ret)
        return ret;

    ret = PyDict_New();

    toPy_store_ref(ctx, idx, ret);

    if (!ret)
        RP_THROW(ctx, "failed to create new python Dictionary for translating from input object");

    duk_enum(ctx,-1,0);
    while (duk_next(ctx,-1,1))
    {
        key = duk_get_string(ctx, -2);
        val = type_to_pytype(ctx, -1);
        PyDict_SetItemString(ret, key, val);
        duk_pop_2(ctx);
    }
    duk_pop(ctx);//enum

    return ret;
}

static PyObject * epochms_to_pytime(int64_t ts, duk_context *ctx) 
{
    time_t t = (time_t)(ts/1000);
    int ms = (int)(ts%1000) * 1000;

    struct tm lt, *local_time = &lt;
    
    localtime_r(&t,&lt);

    if (!PyDateTimeAPI) { 
        PyDateTime_IMPORT; 
        if(!PyDateTimeAPI){
            char buf[MAX_EXCEPTION_LENGTH];
            RP_THROW(ctx, get_exception(buf) ); 
        }
    }

    return PyDateTime_FromDateAndTime(
        local_time->tm_year + 1900,
        local_time->tm_mon +1,
        local_time->tm_mday,
        local_time->tm_hour,
        local_time->tm_min,
        local_time->tm_sec,
        ms
    );
}

//requires a js date at idx, no checks are performed
static int64_t jstime_to_epochms(duk_context *ctx, duk_idx_t idx)
{
    int64_t t=0;

    //call mydate.getTime()
    duk_dup(ctx, idx);
    duk_push_string(ctx, "getTime");

    if(duk_pcall_prop(ctx, -2, 0)==DUK_EXEC_SUCCESS)
    {
        t=(int64_t)duk_get_number_default(ctx, -1, 0);
    }
    duk_pop_2(ctx);

    return t;
}

static PyObject * obj_to_pytype(duk_context *ctx, duk_idx_t idx)
{
    /* four possibilities:
        1) A proper object
        2) Time/Date
        3) An array
        4) An "translation" object like
            {
                pytype:['dictionary', 'list', 'tuple','float', 'integer', 'complex' 'string', 'boolean', 'byte', 'bytearray', 'none']
                pyvalue: the value to be converted
            }
        Check in order: array, time, translation object and default to proper object 
    */

    if(duk_is_function(ctx, idx))
        RP_THROW(ctx, "cannot convert a javascript function to python variable");

    // 1) ARRAY
    if( duk_is_array(ctx, idx) )
        return array_to_pytuple(ctx, idx);
    // 2) TIME
    else if( duk_has_prop_string(ctx, idx, "getMilliseconds") && duk_has_prop_string(ctx, idx, "getUTCDay") )
    {
        return epochms_to_pytime(jstime_to_epochms(ctx, idx), ctx);
    }
    // 3 TRANSLATION OBJECT
    else if( duk_has_prop_string(ctx, idx, "pyType") && duk_has_prop_string(ctx, idx, "value") )
    {
        PyObject *ret=NULL;
        duk_int_t jstype;
        const char *type;

        duk_get_prop_string(ctx, idx, "pyType");
        type=duk_get_string(ctx, -1);
        duk_pop(ctx);

        duk_get_prop_string(ctx, idx, "value");
        jstype=duk_get_type(ctx, -1);
        //pop after conversion below

        if(duk_is_function(ctx, -1))
            RP_THROW(ctx, "cannot convert a javascript function to python variable");

        if (!strcasecmp(type,"boolean") || !strcasecmp(type,"bool"))
        {
            ret = PyBool_FromLong( (long) duk_to_boolean(ctx, -1) );
        }
        else if (!strcasecmp(type,"float"))
        {
            ret = PyFloat_FromDouble( (double)duk_to_number(ctx, -1) );
        }
        else if (!strcasecmp(type,"int") || !strcasecmp(type,"integer"))
        {
            if(jstype==DUK_TYPE_STRING)
            {
                int base=10;
                if(duk_get_prop_string(ctx, idx, "pyBase"))
                {
                    if(duk_is_number(ctx, -1))
                        base = duk_get_number(ctx, -1);
                }
                duk_pop(ctx);
                ret=str_to_pyint(ctx, -1, base);
            }
            else if (jstype == DUK_TYPE_NUMBER)
                ret=num_to_pyint(ctx, -1);
            else
                ret=PyLong_FromLong(duk_to_int(ctx, -1));
                
        }
        else if (!strcasecmp(type,"string"))
        {
            if (jstype==DUK_TYPE_BUFFER)
                duk_buffer_to_string(ctx, -1);
            else if(jstype!=DUK_TYPE_STRING)
                duk_json_encode(ctx, -1);
            ret=str_to_pystr(ctx, -1);
        }
        else if (!strcasecmp(type,"complex"))
        {
            if(jstype==DUK_TYPE_NUMBER)
                ret=PyComplex_FromDoubles((double)duk_get_number(ctx, -1), 0.0);
            else if (duk_is_array(ctx, -1))
                ret=array_to_pycomplex(ctx, -1);
            else
                RP_THROW(ctx, "can't translate value to PyComplex number.  Must be an array or a single number");
        }
        else if (!strcasecmp(type,"bytes"))
        {
            if(jstype!=DUK_TYPE_STRING && jstype!=DUK_TYPE_BUFFER)
                duk_json_encode(ctx, -1);
            if(jstype==DUK_TYPE_STRING)
                duk_to_buffer(ctx, -1, NULL);
            ret=buf_to_pybytes(ctx, -1);
        }
        else if (!strcasecmp(type,"bytearray"))
        {
            if(jstype!=DUK_TYPE_STRING && jstype!=DUK_TYPE_BUFFER)
                duk_json_encode(ctx, -1);
            if(jstype==DUK_TYPE_STRING)
                duk_to_buffer(ctx, -1, NULL);
            ret=buf_to_pybytearray(ctx, -1);
        }
        else if (!strcasecmp(type,"tuple"))
        {
            if(!duk_is_array(ctx, -1))
            {
                duk_push_array(ctx);
                duk_pull(ctx, -2);
                duk_put_prop_index(ctx, -2, 0);
            }
            ret=array_to_pytuple(ctx, -1);
        }
        else if (!strcasecmp(type,"list"))
        {
            if(!duk_is_array(ctx, -1))
            {
                duk_push_array(ctx);
                duk_pull(ctx, -2);
                duk_put_prop_index(ctx, -2, 0);
            }
            ret=array_to_pylist(ctx, -1);
        }
        else if (!strcasecmp(type,"dictionary") || !strcasecmp(type,"dict"))
        {
            if(!duk_is_object(ctx, -1))
            {
                duk_push_array(ctx);
                duk_pull(ctx, -2);
                duk_put_prop_index(ctx, -2, 0);
            }
            ret=obj_to_pydict(ctx, -1);
        }
        else if (!strcasecmp(type,"datetime") || !strcasecmp(type,"date"))
        {
            if(jstype==DUK_TYPE_NUMBER)
                ret=epochms_to_pytime((int64_t)duk_to_number(ctx, -1), ctx);
            else if( 
                jstype==DUK_TYPE_OBJECT &&
                duk_has_prop_string(ctx, -1, "getMilliseconds") && 
                duk_has_prop_string(ctx, -1, "getUTCDay") 
            )
                ret=epochms_to_pytime(jstime_to_epochms(ctx, -1), ctx);
            else
                RP_THROW(ctx, "cannot convert value to a python datetime");
        }
        else
        {
            RP_THROW(ctx, "Unknown conversion type (pytype='%s')", type);
        }

        duk_pop(ctx);

        if(ret==NULL)
            Py_RETURN_NONE;
        else
            return ret;

    }
    // 4) PROPER OBJECT
    else
    {
        return obj_to_pydict(ctx, idx);
    }
}

static PyObject * type_to_pytype(duk_context *ctx, duk_idx_t idx)
{
    duk_int_t type=duk_get_type(ctx, idx);

    idx=duk_normalize_index(ctx, idx);

    switch(type)
    {
        case DUK_TYPE_BOOLEAN:
            return bool_to_pybool(ctx, idx);
        case DUK_TYPE_NUMBER:
            return num_to_pyfloat(ctx, idx);
        case DUK_TYPE_STRING:
            return str_to_pystr(ctx, idx);
        case DUK_TYPE_BUFFER:
            return buf_to_pybytes(ctx, idx);
        case DUK_TYPE_OBJECT:
            if (duk_is_function(ctx, -1) || duk_is_c_function(ctx, -1) )
                Py_RETURN_NONE;
            return obj_to_pytype(ctx, idx);
        case DUK_TYPE_UNDEFINED:
        case DUK_TYPE_NULL:
        default:
            Py_RETURN_NONE;
    }
}

/* ************** END JSVAR -> PYTHON VAR ******************* */

/* ****************** PYTHON VAR -> JSVAR ******************* */

static void push_ptype(duk_context *ctx, PyObject * pyvar);

static int tojs_check_ref(duk_context *ctx, PyObject *pobj)
{
    void *hptr=NULL;
    char ptr_str[32];

    snprintf(ptr_str,32,"%p", pobj); //the key to store/retrieve the js heap pointer
    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("pytojsmap"));

    if(duk_get_prop_string(ctx, -1, ptr_str))
    {
        hptr=duk_get_pointer(ctx, -1);
        duk_pop_2(ctx);
        duk_push_heapptr(ctx, hptr);
        return 1;
    }
    duk_pop_2(ctx);
    return 0;
}

static void tojs_store_ref(duk_context *ctx, duk_idx_t idx, PyObject *pobj)
{
    char ptr_str[32];
    void *hptr = duk_get_heapptr(ctx, idx);

    snprintf(ptr_str,32,"%p", pobj); //the key to store/retrieve the PyObject pointer

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("pytojsmap"));

    duk_push_pointer(ctx, hptr);
    duk_put_prop_string(ctx, -2, ptr_str);
    duk_pop(ctx);
}

static void push_dict_to_object(duk_context *ctx, PyObject *pobj)
{
    Py_ssize_t pos=0;
    PyObject *key=NULL, *value=NULL;

    if (tojs_check_ref(ctx, pobj))
        return;

    duk_push_object(ctx);

    tojs_store_ref(ctx, -1, pobj);

    while( PyDict_Next(pobj, &pos, &key, &value) )
    {
        duk_push_string(ctx, PyUnicode_AsUTF8(key));
        push_ptype(ctx, value);
        duk_put_prop(ctx, -3);        
    }
}

static void push_tuple_to_array(duk_context *ctx, PyObject *pobj)
{
    Py_ssize_t len=PyTuple_Size(pobj), i=0;
    PyObject *value=NULL;

    if (tojs_check_ref(ctx, pobj))
        return;

    duk_push_array(ctx);

    tojs_store_ref(ctx, -1, pobj);

    while( i<len )
    {
        value=PyTuple_GetItem(pobj, i);
        push_ptype(ctx, value);
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);        
        i++;
    }
}

static void push_list_to_array(duk_context *ctx, PyObject *pobj)
{
    Py_ssize_t len=PyList_Size(pobj), i=0;
    PyObject *value=NULL;

    if (tojs_check_ref(ctx, pobj))
        return;

    duk_push_array(ctx);

    tojs_store_ref(ctx, -1, pobj);

    while( i<len )
    {
        value=PyList_GetItem(pobj, i);
        push_ptype(ctx, value);
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);        
        i++;
    }
}



static void push_ptype_to_string(duk_context *ctx, PyObject * pyvar)
{

    if( PyUnicode_Check(pyvar) )
        duk_push_string(ctx, PyUnicode_AsUTF8(pyvar));

    else
    {
        PyObject *pystr = PyObject_Str(pyvar);
        if(!pystr)
        {
            char buf[MAX_EXCEPTION_LENGTH];
            RP_THROW(ctx, "error converting python return value as a string: %s",get_exception(buf));
        }
        duk_push_string(ctx, PyUnicode_AsUTF8(pystr));
        RP_Py_XDECREF(pystr);
    }
}


static void push_ptype(duk_context *ctx, PyObject * pyvar)
{
    if( !pyvar )
        duk_push_null(ctx);
    else if( pyvar == Py_None )
        duk_push_null(ctx);
    else if( PyBool_Check(pyvar) )
        duk_push_boolean(ctx, (pyvar==Py_True));
    else if( PyUnicode_Check(pyvar) )
        duk_push_string(ctx, PyUnicode_AsUTF8(pyvar));
    else if( PyFloat_Check(pyvar) )
        duk_push_number(ctx, (duk_double_t) PyFloat_AsDouble(pyvar) );
    else if ( PyLong_Check(pyvar) )
    {
        double n = PyLong_AsDouble(pyvar);

        if( PyErr_Occurred() )
        {
            if ( PyErr_ExceptionMatches(PyExc_OverflowError) )
            {
                push_ptype_to_string(ctx, pyvar);
                PyErr_Clear();
            }
            else
            {
                char buf[MAX_EXCEPTION_LENGTH];
                RP_THROW(ctx, "error converting return python integer:\n %s",get_exception(buf));
            }
        }
        else
        {
            duk_push_number(ctx, (duk_double_t)n);
        }
    }
    else if( PyList_Check(pyvar) )
        push_list_to_array(ctx, pyvar);
    else if( PyTuple_Check(pyvar) )
        push_tuple_to_array(ctx, pyvar);
    else if( PyDict_Check(pyvar) )
        push_dict_to_object(ctx, pyvar);
    else if( PyBytes_Check(pyvar) )
    {
        Py_ssize_t l=0;
        char *b;
        void *jb;

        PyBytes_AsStringAndSize(pyvar, &b, &l);
        jb=duk_push_fixed_buffer(ctx, (duk_size_t)l);
        memcpy(jb,b,(size_t)l);        
    }
    else if( PyComplex_Check(pyvar) )
    {
        duk_push_array(ctx);
        duk_push_number(ctx, PyComplex_RealAsDouble(pyvar));
        duk_put_prop_index(ctx, -2, 0);
        duk_push_number(ctx, PyComplex_ImagAsDouble(pyvar));
        duk_put_prop_index(ctx, -2, 1);
    }
    else if( PyByteArray_Check(pyvar) )
    {
        PyObject *pbytes = PyBytes_FromObject(pyvar);
        Py_ssize_t l=0;
        char *b;
        void *jb;

        PyBytes_AsStringAndSize(pbytes, &b, &l);
        jb=duk_push_fixed_buffer(ctx, (duk_size_t)l);
        memcpy(jb,b,(size_t)l);
        RP_Py_XDECREF(pbytes);
    }
    // kinda odd, but Pydate_Check segfaults if just checking and pydatetimeapi isn't imported
    else {
        if (!PyDateTimeAPI) { 
            PyDateTime_IMPORT; 
            if(!PyDateTimeAPI){
                char buf[MAX_EXCEPTION_LENGTH];
                RP_THROW(ctx, get_exception(buf) ); 
            }
        }
        if( PyDate_Check(pyvar) )
        {
            struct tm ts = { 0 }, gtm = {0};
            int ms;
            time_t time, gtime;
            int64_t time64;
            ts.tm_year  = PyDateTime_GET_YEAR(pyvar) -1900;
            ts.tm_mon   = PyDateTime_GET_MONTH(pyvar) -1;
            ts.tm_mday  = PyDateTime_GET_DAY(pyvar);
            ts.tm_hour  = PyDateTime_DATE_GET_HOUR(pyvar);
            ts.tm_min   = PyDateTime_DATE_GET_MINUTE(pyvar);
            ts.tm_sec   = PyDateTime_DATE_GET_SECOND(pyvar);
            ms          = PyDateTime_DATE_GET_MICROSECOND(pyvar);
            
            time=mktime(&ts);
            gmtime_r(&time, &gtm);
            gtime=mktime(&gtm);
            time64 = time * 1000 + ms/1000 - (gtime-time)*1000;

            duk_get_global_string(ctx, "Date");
            duk_push_number(ctx, (duk_double_t)time64);
            duk_new(ctx, 1);        
        }
        else // whatever else
            push_ptype_to_string(ctx, pyvar);
    }    
}

/* ************** END PYTHON VAR -> JSVAR ******************* */

/* ************** PYTHON FORKING **************************** */


#define PFI struct python_fork_info_s
PFI
{
    int reader;         // pipe to read from, in parent or child
    int writer;         // pipe to write to, in parent or child
    pid_t childpid;     // process id of the child if in parent (return from fork())
    duk_context *ctx;   // the ctx to use in the fork (same as current thread)
};

PFI **pyforkinfo = NULL;
static int n_pfi=0;

static PFI *check_fork();

#define forkwrite(b,c) ({\
    int r=0;\
    r=write(finfo->writer, (b),(c));\
    if(r==-1) {\
        fprintf(stderr, "fork write failed: '%s' at %d, fd:%d\n",strerror(errno),__LINE__,finfo->writer);\
        if(is_child) {fprintf(stderr, "child proc exiting\n");exit(0);}\
    };\
    r;\
})

#define forkread(b,c) ({\
    int r=0;\
    r= read(finfo->reader,(b),(c));\
    if(r==-1) {\
        fprintf(stderr, "fork read failed: '%s' at %d\n",strerror(errno),__LINE__);\
        if(is_child) {fprintf(stderr, "child proc exiting\n");exit(0);}\
    };\
    r;\
})

// whether this thread is/should be using a forked child to interact with python
#define must_fork (!RPTHR_TEST(get_current_thread(), RPTHR_FLAG_THR_SAFE))

// set flag after we have a child process for this thread
#define set_forked RPTHR_SET(get_current_thread(), RPTHR_FLAG_FORKED)

// clear flag after we have killed a child process for this thread
#define clear_forked RPTHR_CLEAR(get_current_thread(), RPTHR_FLAG_FORKED)

// check if we have (or should have) a child process for this thread
#define is_forked (RPTHR_TEST(get_current_thread(), RPTHR_FLAG_FORKED))

static void py_kill_child(void *arg)
{
    pid_t *kpid = (pid_t*)arg;
    kill(*kpid,SIGTERM);    
    dprintf("killed child %d\n",(int)*kpid);
    clear_forked; // kinda pointless now, since this is not called unless thread dies
                  // when all flags are cleared.  May be needed later.
}


void parent_finalizer(PyObject *pModule)
{
    PFI *finfo = check_fork();
    duk_context *ctx;
    char retval='X';
    char *pipe_error="parent_finalizer: pipe error";

    if(!finfo)
        RP_THROW( (get_current_thread())->ctx, "parent_finalizer: error retrieving fork information.");

    ctx=finfo->ctx;

    if(forkwrite("f", sizeof(char)) == -1)
        RP_THROW(ctx, pipe_error);

    if(forkwrite(&pModule, sizeof(PyObject *)) == -1)
        RP_THROW(ctx, pipe_error);

    if(forkread(&retval, sizeof(char)) == -1)
        RP_THROW(ctx, pipe_error);

    if(retval != 'o')
        RP_THROW(ctx, pipe_error);
}

int child_finalizer(PFI *finfo)
{
    PyObject *pModule=NULL;

    if(forkread(&pModule, sizeof(PyObject *)) == -1)
        return 0;

    RP_Py_XDECREF(pModule);
    dprintf("Finalized in child\n");
    if(forkwrite("o", sizeof(char)) == -1)
        return 0;

    return 1;    
}

static duk_ret_t named_call(duk_context *ctx);

static void put_mod_funcs_from_string(duk_context *ctx, PyObject *pModule, char *s)
{
    char *end=strchr(s, ' ');

    while( 1 )
    {

        duk_push_c_function(ctx, named_call, DUK_VARARGS);

        // hidden pmod as property of function
        duk_push_pointer(ctx, (void*)pModule);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pMod"));

        // save our calling thread
        duk_push_int(ctx, get_thread_num() );
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("thrno"));

        //terminate s
        if(end)
            *end='\0';

        // name of this function
        duk_push_string(ctx, s);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pyfunc_name"));                

        // s as property of return object
        duk_put_prop_string(ctx, -2, s);
        
        if(end)
        {
            s=end+1;
            if(!*s)
                break;
        }
        else
            break;

        end=strchr(s, ' ');
    }
}

PyObject *parent_importType(const char *script, char *type, char **fnames)
{
    PFI *finfo = check_fork();
    size_t script_sz = strlen(script)+1; //include the null
    PyObject *pModule=NULL;
    char status='\0';

    if(!finfo)
        return NULL;

    // type is "s" or "i".  See do_fork_loop below
    if(forkwrite(type, sizeof(char)) == -1)
        return NULL;

    if(forkwrite(&script_sz, sizeof(size_t)) == -1)
        return NULL;

    if(forkwrite(script, script_sz) == -1)
        return NULL;

    if(forkread(&pModule, sizeof(PyObject *)) == -1)
        return NULL;

    dprintf("in parent got pmod=%p\n",pModule);

    if(forkread(&status, sizeof(char)) == -1)
        return NULL;

    if(status == 'o')
    {
        char *funcnames=NULL;
        size_t flen=0;

        if(forkread(&flen, sizeof(size_t)) == -1)
            return NULL;

        REMALLOC(funcnames, flen);
        if(forkread(funcnames, flen) == -1)
        {
            free(funcnames);
            return NULL;
        }        
        *fnames = funcnames;
        return pModule;
    }
    else if(status == 'e')
    {
        char *pErr=NULL;
        size_t pErr_sz=0;

        if(forkread(&pErr_sz, sizeof(size_t)) == -1)
            return NULL;

        REMALLOC(pErr, pErr_sz);
        if(forkread(pErr, pErr_sz) == -1)
        {
            free(pErr);
            return NULL;
        }        
        duk_push_string(finfo->ctx, pErr);
        free(pErr);
        *fnames=NULL;
    }

    return pModule;
}

PyObject *parent_importString(const char *script, char **fnames)
{
    return parent_importType(script, "s", fnames);
}

PyObject *parent_import(const char *script, char **fnames)
{
    return parent_importType(script, "i", fnames);
}

char *stringify_funcnames(PyObject* pModule)
{
    PyObject *pobj = PyObject_Dir(pModule);

    dprintf("%schecking for functions in object. type: %s\n", pobj?"":"FAIL ", Py_TYPE(pModule)->tp_name);

    if(!pobj)
    {
        char buf[MAX_EXCEPTION_LENGTH];
        const char *exc = get_exception(buf);
        dprintf("pyobject_dir exception: %s\n", exc);        
        (void)exc;
        return strdup("");
    }

    Py_ssize_t len = PyList_Size(pobj), i=0;
    PyObject *value=NULL, *pFunc;
    char *str=strdup("");

    while( i<len )
    {
        value=PyList_GetItem(pobj, i);
        pFunc = PyObject_GetAttr(pModule, value);
        /* inherited funcs in a class come up as null (at least the first time) */
        dprintf("checking %s - is %s - iscallable:%d\n", PyUnicode_AsUTF8(value), (pFunc? Py_TYPE(pFunc)->tp_name:"pFunc=NULL"), pFunc ? PyCallable_Check(pFunc):0);
        if(!pFunc)
        {
            PyObject* pBase = (PyObject*) pModule->ob_type->tp_base;
            if(pBase)
            {
                pFunc = PyObject_GetAttr(pBase, value);
                dprintf("Got from base, pFunc=%p\n", pFunc);
            }
        }
        if(pFunc && PyCallable_Check(pFunc))
        {
            const char *fname = PyUnicode_AsUTF8(value);
            str = strcatdup(str, (char *)fname);
            str = strcatdup(str, " ");
        }
        i++;
        RP_Py_XDECREF(pFunc);
    }
    return str;
}



int child_importString(PFI *finfo)
{
    size_t script_sz;
    char *script=NULL;
    const char *exc=NULL;
    PyObject *pModule=NULL, *pCode=NULL;
    duk_context *ctx = finfo->ctx;
    char *modname, *s, *freeme=NULL;
    char buf[MAX_EXCEPTION_LENGTH];

    dprintf("in child_importString\n");
    if (forkread(&script_sz, sizeof(size_t))  == -1)
    {
        dprintf("fail read script_sz");
        forkwrite(pModule, sizeof(PyObject*)); //Send NULL
        return 0;
    }

    REMALLOC(script, script_sz);
    freeme=script;

    if (forkread(script, script_sz)  == -1)
    {
        dprintf("fail read script");
        forkwrite(pModule, sizeof(PyObject*));
        return 0;
    }

    if( (s=strchr(script, '\xff')) )
    {
        modname=script;
        script=s+1;
        *s='\0';
    }
    else modname="module_from_string";

    dprintf("in child_importString - python_is_init=%d\n",python_is_init);
    if(!python_is_init)
        rp_duk_python_init(finfo->ctx);

    RP_EMPTY_STACK(ctx);

    pCode = Py_CompileString(script, modname, Py_file_input);
    dprintf("in child_importString - pCode = %p\n", pCode);

    if(!pCode)
    {
        exc = get_exception(buf);
        dprintf("child_importString: !pCode: %s\n",exc);
    }
    else
    {
        dprintf("in child_importString -PyImport_ExecCodeModule(%s, %p)\n",modname, pCode);
        pModule = PyImport_ExecCodeModule(modname, pCode );
        dprintf("in child_importString - pModule = %p\n", pModule);
        if(!pModule)
        {
            exc = get_exception(buf);
            dprintf("child_importString: !pModule: %s\n",exc);
        }
    }

    duk_push_string(ctx, script);
    free(freeme);

    if(forkwrite(&pModule, sizeof(PyObject*)) == -1)
        return 0;

    if(!exc)
    {
        char *funcnames = stringify_funcnames(pModule);
        size_t flen = strlen(funcnames)+1;

        if(forkwrite("o", sizeof(char)) == -1)
            return 0;
        if(forkwrite(&flen, sizeof(size_t)) == -1)
            return 0;
        if(forkwrite(funcnames, flen) == -1)
            return 0;
        free(funcnames);
    }
    else
    {
        size_t exc_sz=strlen(exc)+1;

        if(forkwrite("e", sizeof(char)) == -1)
            return 0;

        if(forkwrite(&exc_sz, sizeof(size_t)) == -1)
            return 0;

        if(forkwrite(exc, exc_sz) == -1)
            return 0;
    }

    return 1;
}


/* parent_import and parent_importString are above */

int child_import(PFI *finfo)
{
    size_t script_sz;
    char *script=NULL;
    const char *exc=NULL;
    PyObject *pModule=NULL;
    duk_context *ctx = finfo->ctx;
    char buf[MAX_EXCEPTION_LENGTH];

    dprintf("in child_import\n");
    if (forkread(&script_sz, sizeof(size_t))  == -1)
    {
        dprintf("fail read script_sz");
        forkwrite(pModule, sizeof(PyObject*)); //Send NULL
        return 0;
    }

    REMALLOC(script, script_sz);

    if (forkread(script, script_sz)  == -1)
    {
        dprintf("fail read script");
        forkwrite(pModule, sizeof(PyObject*));
        return 0;
    }

    dprintf("in child_import - python_is_init=%d\n",python_is_init);
    if(!python_is_init)
        rp_duk_python_init(finfo->ctx);

    RP_EMPTY_STACK(ctx);

    pModule = PyImport_ImportModule(script);

    if(!pModule)
        exc = get_exception(buf);


    if(forkwrite(&pModule, sizeof(PyObject*)) == -1)
        return 0;
    if(!exc)
    {
        char *funcnames = stringify_funcnames(pModule);
        size_t flen = strlen(funcnames)+1;

        if(forkwrite("o", sizeof(char)) == -1)
            return 0;
        if(forkwrite(&flen, sizeof(size_t)) == -1)
            return 0;
        if(forkwrite(funcnames, flen) == -1)
            return 0;
        free(funcnames);
    }
    else
    {
        size_t exc_sz=strlen(exc)+1;

        if(forkwrite("e", sizeof(char)) == -1)
            return 0;

        if(forkwrite(&exc_sz, sizeof(size_t)) == -1)
            return 0;

        if(forkwrite(exc, exc_sz) == -1)
            return 0;

    }

    return 1;
}

#define pyobj_to_bytes(pbytes,obj,sz) ({\
    char *_res;\
    PyObject *pbytes=PyMarshal_WriteObjectToString((obj),Py_MARSHAL_VERSION);\
    PyBytes_AsStringAndSize( pbytes, &_res, &(sz) );\
    (sz)++;\
    RP_Py_XDECREF(pbytes);\
    _res;\
})


static duk_ret_t _p_to_string(duk_context *ctx);
static duk_ret_t _p_to_value(duk_context *ctx);
static duk_ret_t pvalue_finalizer(duk_context *ctx);

static duk_ret_t _get_pref_str(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("prefstr"));
    return 1;
}

char *parent_py_call_read_error(PFI *finfo)
{
    duk_size_t sz;
    char *errmsg=NULL;

    if(forkread(&sz, sizeof(duk_size_t)) == -1)
        return NULL;

    REMALLOC(errmsg, (size_t)sz);

    if(forkread(errmsg, sz) == -1)
    {
        free(errmsg);
        return NULL;
    }
    return errmsg;
}


char *parent_py_call(PyObject * pModule, const char *fname)
{
    PFI *finfo = check_fork();
    PyObject *pArgs=NULL, *pValue=NULL, *pBytes=NULL;
    duk_context *ctx;
    duk_idx_t i=1, top;
    char *marshal=NULL;
    Py_ssize_t marshal_sz=0;
    size_t fname_sz = strlen(fname)+1;
    char retval='X';
    char *pipe_error="parent_py_call: pipe error";
    char *errmsg=NULL;
    PyGILState_STATE state;

    if(!finfo)
        return "parent_py_call: error retrieving fork information.";

    ctx=finfo->ctx;
    top=duk_get_top(ctx);

    dprintf("parent_py_call, top=%d\n", (int)top);

    if( i<top) //we have arguments
    {
        PyObject **pPtrs = NULL, *kwdict=NULL;
        int npPtrs=0;

        state = PYLOCK;

        kwdict=PyDict_New(); //this holds named parameters/keyword args to function

        // create the jsobj->pyobj map.  Used to detect and fix cyclical references
        duk_push_global_object(ctx);
        duk_push_object(ctx);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("jstopymap")); 
        duk_pop(ctx);

        // put keyword args into kwdict
        while(i<top)
        {   //if in javascript we get the call pysomething.dosomething({pyargs: {arg1:val1,arg2:val2}})
            if( duk_is_object(ctx, i) && duk_has_prop_string(ctx, i, "pyArgs") )
            {
                PyObject *val=NULL;
                const char *key;

                duk_get_prop_string(ctx, i, "pyArgs");
                kwdict=PyDict_New();
                duk_enum(ctx,-1,0);
                while (duk_next(ctx,-1,1))
                {
                    key = duk_get_string(ctx, -2);
                    if( duk_is_object(ctx,-1) && 
                        duk_has_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pvalue")) 
                    )
                    {
                        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pvalue"));
                        val=(PyObject *)duk_get_pointer(ctx, -1);
                        // pydict_setitem DOES NOT steal a reference. Go Figure. 
                        // so dont do: Py_XINCREF(pValue);
                        duk_pop(ctx);
                    }
                    else if( duk_is_object(ctx,-1) && 
                        duk_has_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pref")) 
                    )
                    {
                        //"pref" is a pointer obtained from and valid in this thread's child proc
                        //store it as a long in a dictionary so it is marshalable
                        void *refptr = NULL;

                        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pref"));
                        refptr = duk_get_pointer(ctx, -1);
                        duk_pop(ctx);

                        REMALLOC(pPtrs, sizeof(PyObject *) * (npPtrs + 1));
                        pPtrs[npPtrs] = PyLong_FromVoidPtr(refptr);
                        val = PyDict_New();
                        PyDict_SetItemString(val, "___REF_IN_CHILD___", pPtrs[npPtrs]);
                        dprintf("PUT pRef from storage in PyObj, sending pRef=%p\n", refptr);
                        npPtrs++;
                    }
                    else //its a js var, so convert to py var
                        val=type_to_pytype(ctx, -1);

                    PyDict_SetItemString(kwdict, key, val);
                    duk_pop_2(ctx);
                }
                duk_pop_2(ctx);//enum, pykeyword value

                duk_remove(ctx, i); // pykeyword object
                break;// only do this once.
            }
            i++;
        }

        i=1;
        top=duk_get_top(ctx);

        pArgs = PyTuple_New((int)top); // one less because we are starting at index 1, one extra for kwdict, even if it is empty

        //iterate through parameters, starting with the second.
        while (i<top)
        {
            // check if this is a variable returned from this function, with hidden pyobject in it.
            if( duk_is_object(ctx,i) && 
                duk_has_prop_string(ctx, i, DUK_HIDDEN_SYMBOL("pvalue")) 
            )
            {
                duk_get_prop_string(ctx, i, DUK_HIDDEN_SYMBOL("pvalue"));
                pValue=(PyObject *)duk_get_pointer(ctx, -1);
                // pytuple_setitem steals reference.  We have to increase to keep it
                Py_XINCREF(pValue);
                duk_pop(ctx);
            }
            // check if this is a reference to a pyobject stored in child proc
            else if( duk_is_object(ctx,i) && 
                duk_has_prop_string(ctx, i, DUK_HIDDEN_SYMBOL("pref")) 
            )
            {
                //store it as a long in a dictionary
                void *refptr = NULL;

                duk_get_prop_string(ctx, i, DUK_HIDDEN_SYMBOL("pref"));
                refptr = duk_get_pointer(ctx, -1);
                duk_pop(ctx);

                REMALLOC(pPtrs, sizeof(PyObject *) * (npPtrs + 1));
                pPtrs[npPtrs] = PyLong_FromVoidPtr(refptr);
                pValue = PyDict_New();
                PyDict_SetItemString(pValue, "___REF_IN_CHILD___", pPtrs[npPtrs]);
                dprintf("PUT pRef from storage in PyObj, sending pRef=%p\n", refptr);
                npPtrs++;
            }
            else
                pValue=type_to_pytype(ctx, i);

            if(pValue==NULL)
            {
                PYUNLOCK(state);
                return "parent_py_call: error setting pValue";
            }

            PyTuple_SetItem(pArgs, (int)i-1, pValue);
            i++;
        }

        //add the kwdict at the end of tuple
        PyTuple_SetItem(pArgs, (int)i-1, kwdict);

        dprintf("pArgs = %p, %s\n", pArgs, PyUnicode_AsUTF8(PyObject_Str(pArgs)) );

        pBytes=PyMarshal_WriteObjectToString(pArgs,Py_MARSHAL_VERSION);
        if(!pBytes)
        {
            char buf[MAX_EXCEPTION_LENGTH];
            fprintf(stderr, "exception: %s\n", get_exception(buf));
            abort();
        }
        PyBytes_AsStringAndSize( pBytes, &marshal, &marshal_sz );
        marshal_sz++;

        RP_Py_XDECREF(pArgs);
        if(pPtrs)
        {
            for (i=0;i<npPtrs;i++)
                RP_Py_XDECREF(pPtrs[i]);
            free(pPtrs);
        }
        PYUNLOCK(state);
    }


    if(forkwrite("p", sizeof(char)) == -1)
        return pipe_error;

    if(forkwrite(&pModule, sizeof(PyObject *)) == -1)
        return pipe_error;

    if(forkwrite(&fname_sz, sizeof(size_t)) == -1)
        return pipe_error;

    if(forkwrite(fname, fname_sz) == -1)
        return pipe_error;

    if(forkwrite(&marshal_sz, sizeof(duk_size_t)) == -1)
        return pipe_error;

    if(marshal_sz)
    {
        dprintf("in parent_py_call - sending: marshal=%p, marshal_sz=%d\n",marshal, (int)marshal_sz);
        //dprintf("reader=%d, writer=%d\n", finfo->reader, finfo->writer);
        dprintfhex(marshal, marshal_sz, "marshal=0x");
        if(forkwrite(marshal, marshal_sz) == -1)
        {
            state = PYLOCK;
            RP_Py_XDECREF(pBytes);
            PYUNLOCK(state);

            return pipe_error;
        }
        state = PYLOCK;
        RP_Py_XDECREF(pBytes);
        PYUNLOCK(state);
        marshal=NULL;
        marshal_sz=0;
    }

    if(forkread(&retval, sizeof(char)) == -1)
        return pipe_error;

    if(retval == 'e')
    {
        errmsg=parent_py_call_read_error(finfo);

        if(!errmsg)
            return pipe_error;

        // RP_THROW with a free in the middle
        duk_push_error_object(ctx, DUK_ERR_SYNTAX_ERROR, "%s", errmsg);
        free(errmsg);
        (void) duk_throw(ctx);\
    }
    else if (retval=='v')
    {
        char *errmsg=NULL;
        marshal_sz=0;
        marshal=NULL;
        pValue=NULL;

        if(forkread(&marshal_sz, sizeof(Py_ssize_t)) == -1)
            return pipe_error;

        REMALLOC(marshal, (size_t)marshal_sz);

        if(forkread(marshal, marshal_sz) == -1)
            return pipe_error;

        dprintf("in parent, marshal_sz=%d\n", (int)marshal_sz);
        state = PYLOCK;
        pValue=PyMarshal_ReadObjectFromString((const char*)marshal, marshal_sz);
        PYUNLOCK(state);
        dprintf("in parent, pValue=%p\n", pValue);
        free(marshal);

        duk_push_global_object(ctx);
        duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("jstopymap"));
        duk_pop(ctx);

        duk_push_object(ctx); //return value as object

        duk_push_pointer(ctx, pValue);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pvalue"));

        duk_push_c_function(ctx, pvalue_finalizer, 1);
        duk_set_finalizer(ctx, -2);
        
        duk_push_c_function(ctx, _p_to_string, 0);
        duk_put_prop_string(ctx, -2, "toString");

        duk_push_c_function(ctx, _p_to_value, 0);
        duk_put_prop_string(ctx, -2, "toValue");

        errmsg=parent_py_call_read_error(finfo);
        if(!errmsg)
            return pipe_error;
        duk_push_string(ctx, errmsg);
        free(errmsg);
        duk_put_prop_string(ctx, -2, "errMsg");
    }
    else if(retval == 'r') // var is non-marshalable and stays in child
    {
        PyObject *pRef=NULL;
        size_t sz=0;
        char *refstr=NULL;
        char *funcnames=NULL;
        size_t flen=0;


        if(forkread(&pRef, sizeof(duk_size_t)) == -1)
            return pipe_error;

        if(forkread(&sz, sizeof(duk_size_t)) == -1)
            return pipe_error;

        REMALLOC(refstr, sz);

        dprintf("receiving refstring, sz=%d\n", (int)sz);
        if(forkread(refstr, sz) == -1)
            return pipe_error;
        dprintf("refstring=%s\n", refstr);
        dprintf("STORING pRef=%p in JS object\n", pRef);

        if(forkread(&flen, sizeof(size_t)) == -1)
            return pipe_error;

        REMALLOC(funcnames, flen);
        if(forkread(funcnames, flen) == -1)
        {
            free(funcnames);
            return pipe_error;
        }

        duk_push_object(ctx); //return value as object

        put_mod_funcs_from_string(ctx, pRef, funcnames);
        free(funcnames);

        duk_push_pointer(ctx, pRef);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pref"));

        duk_push_string(ctx, refstr);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("prefstr"));
        free(refstr);

        duk_push_c_function(ctx, pvalue_finalizer, 1);
        duk_set_finalizer(ctx, -2);
        
        duk_push_c_function(ctx, _get_pref_str, 0);
        duk_put_prop_string(ctx, -2, "toString");

        duk_push_c_function(ctx, _get_pref_str, 0);
        duk_put_prop_string(ctx, -2, "toValue");

        errmsg=parent_py_call_read_error(finfo);
        if(!errmsg)
            return pipe_error;
        duk_push_string(ctx, errmsg);
        free(errmsg);
        duk_put_prop_string(ctx, -2, "errMsg");
    }
    else
    {
        dprintf("in parent_py_call, got bad response '%c' 0x%2x\n", retval, retval);
        //putc(retval,stderr); while (forkread(&retval, sizeof(char)) != -1) putc(retval,stderr);
        return "error: bad response from python child process";
    }
    return NULL;
}

static PyObject *py_call_in_child(duk_context *ctx, char *fname, PyObject *pModule, PyObject *pArgs, PyObject *kwdict)
{
    PyObject *pValue=NULL, *pFunc=NULL;
    char *err=NULL;

    if(pModule == NULL)
    {
        duk_push_string(ctx, "Error: No Module Found");
        return NULL;
    }

    if(!pArgs)
        pArgs=PyTuple_New(0);

    dprintf("in call %p, %s\n", pModule, fname);
    pFunc = PyObject_GetAttrString(pModule, fname);

    dprintf("got func %s(%p) from %s, function callable = %d\n", fname, pFunc, PyUnicode_AsUTF8(PyObject_Str(pModule)), pFunc?(int)PyCallable_Check(pFunc):0);

    if (!pFunc || !PyCallable_Check(pFunc))
    {
        dprintf("error pfunc=%p, callable=%d\n", pFunc, pFunc?(int)PyCallable_Check(pFunc):0);
        err="error calling python function: %s";
        goto end;
    }

    dprintf("CALLING with args=%p, nargs=%d\n", pArgs, (int) (pArgs ? PyTuple_Size(pArgs) : 0) );

    dprintf("tpcall %s, %s, %s\n", PyUnicode_AsUTF8(PyObject_Str(pFunc)), PyUnicode_AsUTF8(PyObject_Str(pArgs)), PyUnicode_AsUTF8(PyObject_Str(kwdict)) );

    //pValue = PyObject_CallObject(pFunc, pArgs);
    pValue = Py_TYPE(pFunc)->tp_call(pFunc, pArgs, kwdict);
    dprintf("CALLED\n");

    if(!pValue) {
        err="error calling python function: %s";
        //goto end;
    }

end:
    dprintf("end: pValue=%p\n",pValue);
    RP_Py_XDECREF(pFunc);
    RP_Py_XDECREF(pArgs);
    RP_Py_XDECREF(kwdict);
    if(err)
    {
        char buf[MAX_EXCEPTION_LENGTH];
        duk_push_sprintf(ctx, err, get_exception(buf));
    }
    else if (PyErr_Occurred())
    {
        char buf[MAX_EXCEPTION_LENGTH];
        char *exc = get_exception(buf);
        duk_push_sprintf(ctx, "Non-fatal error occured while executing '%s': %s", fname, &exc[1]);
    }

    return pValue;
}

static int child_py_call_write_error(PFI *finfo, duk_context *ctx)
{
    duk_size_t sz=0;
    const char *errmsg = duk_get_lstring_default(ctx, -1, &sz, "", 0);
    dprintf("writing error msg: '%s'", errmsg);
    sz++;

    if(forkwrite(&sz, sizeof(duk_size_t)) == -1)
        return 0;

    if(forkwrite(errmsg, sz) == -1)
        return 0;

    return 1;
}


int child_py_call(PFI *finfo)
{
    PyObject *pModule=NULL, *pRes=NULL, *pArgs=NULL, *kwdict=NULL;
    char *fname=NULL;
    duk_context *ctx=finfo->ctx;
    char *marshal=NULL;
    Py_ssize_t marshal_sz=0;
    size_t fname_sz = 0;

    RP_EMPTY_STACK(ctx);

    if(forkread(&pModule, sizeof(PyObject *)) == -1)
        return 0;

    dprintf("in child_py_call, pmod=%p\n", pModule);
    if(forkread(&fname_sz, sizeof(size_t)) == -1)
        return 0;

    REMALLOC(fname, fname_sz);
    if(forkread(fname, fname_sz) == -1)
        return 0;

    if(forkread(&marshal_sz, sizeof(Py_ssize_t)) == -1)
        return 0;

    dprintf("in child_py_call - received, marshal_sz=%d\n", (int)marshal_sz);
    if(marshal_sz > 0)
    {
        REMALLOC(marshal, (size_t) marshal_sz);
        if(forkread(marshal, (size_t)marshal_sz) == -1)
            return 0;

        //dprintf("reader=%d, writer=%d\n", finfo->reader, finfo->writer);
        dprintfhex(marshal, marshal_sz, "marshal=0x");

        pArgs=PyMarshal_ReadObjectFromString((const char*)marshal, marshal_sz);
        free(marshal);

        if(!pArgs || !PyTuple_Check(pArgs) )
        {
            size_t sz=0;
            const char *errmsg = "failed to decode marshal";
            dprintf("writing error %s", errmsg);
            //if( pArgs && !PyTuple_Check(pArgs) ) dprintf("its not null, but its not a tuple");
            sz = strlen(errmsg) + 1;

            if(forkwrite("e", sizeof(char)) == -1)
                return 0;

            if(forkwrite(&sz, sizeof(duk_size_t)) == -1)
                return 0;

            if(forkwrite(errmsg, sz) == -1)
                return 0;

            return 1;
        }
    }

    if(pArgs)
    {    
        Py_ssize_t len=PyTuple_Size(pArgs), i=0, pos=0;
        PyObject *value=NULL, *key=PyUnicode_FromString("___REF_IN_CHILD___");
        PyObject *dkey=NULL, *dvalue=NULL;

        dprintf("pArgs1 = %s\n", PyUnicode_AsUTF8(PyObject_Str(pArgs)) );
        // take the dictionary back out
        kwdict = PyTuple_GetItem(pArgs, len-1);
        Py_XINCREF(kwdict);
        len--;
        _PyTuple_Resize(&pArgs, len);

        dprintf("pArgs = %p, %s\n", pArgs, PyUnicode_AsUTF8(PyObject_Str(pArgs)) );

        // fix references in kwdict by looking for for a dictionary with prop "___REF_IN_CHILD___"
        while( PyDict_Next(kwdict, &pos, &dkey, &dvalue) )
        {
            dprintf("checking %s->%s\n",PyUnicode_AsUTF8(dkey), PyUnicode_AsUTF8(PyObject_Str(dvalue)) ); 
            if( PyDict_Check(dvalue) && PyDict_Contains(dvalue, key/*___REF_IN_CHILD___*/) == 1 )
            {
                 PyObject *pLong = PyDict_GetItem(dvalue, key);        //get the pointer as long
                 PyObject *pRef = (PyObject *)PyLong_AsVoidPtr(pLong); //convert back to pointer
                 dprintf("Got a ref in kwdict, pRef=%p\n", pRef);
                 dprintf("Setting %s to %s\n", PyUnicode_AsUTF8(dkey), PyUnicode_AsUTF8(PyObject_Str(pRef)) );
                 if(PyDict_SetItem(kwdict, dkey, pRef))                //put it as a PyObject in kwdict
                     dprintf("Error HERE\n");
            }
        }

        //look for a dictionary with prop "___REF_IN_CHILD___" in args
        while( i<len )
        {
            value=PyTuple_GetItem(pArgs, i);
            // fix references in arguments
            if( PyDict_Check(value) && PyDict_Contains(value, key) == 1 )
            {
                 PyObject *pLong = PyDict_GetItem(value, key);
                 PyObject *pRef = (PyObject *)PyLong_AsVoidPtr(pLong);
                 dprintf("Got a ref in args, pRef=%p\n", pRef);
                 PyTuple_SetItem(pArgs, i, pRef);
                 Py_XINCREF(pRef);
                 //RP_Py_XDECREF(pLong); -- this is bad.
            }
            i++;
        }
        RP_Py_XDECREF(key);
    }

    pRes=py_call_in_child(ctx, fname, pModule, pArgs, kwdict);
    free(fname);

    if(!pRes)
    {
        if(forkwrite("e", sizeof(char)) == -1)
            return 0;
        if(!child_py_call_write_error(finfo, ctx))
            return 0;
    }
    else
    {
        Py_ssize_t marshal_sz;
        char *marshal;

        //for some reason, we need to do this before attempting to marshal
        //after that PyObject_Dir returns NULL with error
        char *funcnames = stringify_funcnames(pRes);

        dprintf("translating pRes=%p to marshal bytes\n", pRes);
        PyObject *pBytes=PyMarshal_WriteObjectToString(pRes,Py_MARSHAL_VERSION);
        //dprintf("pRes = %s, pBytes=%p\n", PyUnicode_AsUTF8(PyObject_Str(pRes)), pBytes );
        if(!pBytes)
        {
            //char buf[MAX_EXCEPTION_LENGTH];
            //char *exc = get_exception(buf);
            //dprintf("exception %s\n", exc);

            PyErr_Clear(); //clear error from failed marshal

            // this object cannot be marshalled.  We need to store it in this proc
            PyObject *pResStr = PyObject_Str(pRes);
            const char *resstr = PyUnicode_AsUTF8(pResStr);
            size_t str_sz = strlen(resstr) + 1;
            size_t flen = strlen(funcnames)+1;

            dprintf("funcs = %s\n", funcnames);

            if(forkwrite("r", sizeof(char)) == -1)
            {
                RP_Py_XDECREF(pRes);
                RP_Py_XDECREF(pResStr);
                return 0;
            }

            dprintf("SENDING pRef=%p for storage\n", pRes);
            if(forkwrite(&pRes, sizeof(PyObject *)) == -1)
            {
                RP_Py_XDECREF(pRes);
                RP_Py_XDECREF(pResStr);
                return 0;
            }

            if(forkwrite(&str_sz, sizeof(size_t)) == -1)
            {
                RP_Py_XDECREF(pRes);
                RP_Py_XDECREF(pResStr);
                return 0;
            }
            
            //dprintf("sending %s, size=%d\n", resstr, (int)str_sz);
            if(forkwrite(resstr, str_sz) == -1)
            {
                RP_Py_XDECREF(pRes);
                RP_Py_XDECREF(pResStr);
                return 0;
            }

            if(forkwrite(&flen, sizeof(size_t)) == -1)
            {
                RP_Py_XDECREF(pRes);
                RP_Py_XDECREF(pResStr);
                return 0;
            }

            if(forkwrite(funcnames, flen) == -1)
            {
                RP_Py_XDECREF(pRes);
                RP_Py_XDECREF(pResStr);
                return 0;
            }

            free(funcnames);

            RP_Py_XDECREF(pResStr);

            if(!child_py_call_write_error(finfo, ctx))
                return 0;

            return 1;
        }
        free(funcnames);

        dprintf("extracting bytes from pBytes=%p\n", pBytes);
        PyBytes_AsStringAndSize( pBytes, &marshal, &marshal_sz );
        marshal_sz++;

        if(forkwrite("v", sizeof(char)) == -1)
        {
            RP_Py_XDECREF(pRes);
            RP_Py_XDECREF(pBytes);
            return 0;
        }

        if(forkwrite(&marshal_sz, sizeof(Py_ssize_t)) == -1)
        {
            RP_Py_XDECREF(pRes);
            RP_Py_XDECREF(pBytes);
            return 0;
        }
        dprintf("bytes sz=%d\n", (int)marshal_sz);
        if(forkwrite(marshal, marshal_sz) == -1)
        {
            RP_Py_XDECREF(pRes);
            RP_Py_XDECREF(pBytes);
            return 0;
        }

        if(!child_py_call_write_error(finfo, ctx))
            return 0;

        RP_Py_XDECREF(pRes);
        RP_Py_XDECREF(pBytes);
    }

    return 1;
}

static int parent_pid=0;

/* in child process, loop and await commands */
static void do_fork_loop(PFI *finfo)
{
    while(1)
    {
        char command='\0';
        int ret = kill(parent_pid,0);

        if( ret )
            exit(0);

        ret = forkread(&command, sizeof(char));
        if (ret == 0)
        {
            /* a read of 0 size might mean the parent exited, 
               otherwise this shouldn't happen                 */
            usleep(10000);
            continue;
        }

        // commands:
        //   py_call          p
        //   importString     s
        //   import           i
        //   finalizer        f
        dprintf("command='%c'\n", command);
        ret=0;
        switch(command)
        {
            case 'p':
                ret = child_py_call(finfo);
                break;
            case 's':
                ret = child_importString(finfo);
                break;
            case 'i':
                ret = child_import(finfo);
                break;
            case 'f':
                ret = child_finalizer(finfo);
                break;
        }
        if(!ret)
        {
            fprintf(stderr,"error in python fork\n");
            exit(1);
        }
    }
}


//static char *scr_txt = "rampart.utils.fprintf(rampart.utils.stderr,'YA, were in\\n');var p=require('rampart-python');p.__helper();\n";
static char *scr_txt = "var p=require('rampart-python');p.__helper(%d,%d);\n";

static PFI *check_fork()
{
    int pidstatus;
    int threadno = get_thread_num();
    PFI *finfo = NULL;
    dprintf("check_fork(), threadno=%d\n", threadno);

    RPYLOCK;
    if (n_pfi < threadno+1)
    {
        int i=n_pfi;
        //RPYLOCK;
        n_pfi=threadno+1;
        REMALLOC( pyforkinfo, n_pfi * sizeof (PFI*) );
        while(i<n_pfi)
        {
            pyforkinfo[i]=NULL;
            i++;
        }
        //RPYUNLOCK;
    }
    
    finfo = pyforkinfo[threadno];

    if(finfo == NULL)
    {
        if(is_forked)
        {
            fprintf(stderr, "Unexpected Error: previously opened pipe info no longer exists for python forkno %d\n", threadno);
            RPYUNLOCK;
            return NULL;
        }
        else
        {
            //printf("creating finfo for forkno %d\n", h->forkno);
            REMALLOC(pyforkinfo[threadno], sizeof(PFI));
            finfo=pyforkinfo[threadno];
            finfo->reader=-1;
            finfo->writer=-1;
            finfo->childpid=0;
            finfo->ctx = ( get_current_thread() )->ctx;

        }

    }
    RPYUNLOCK;

    /* waitpid: like kill(pid,0) except only works on child processes */
    if (!finfo->childpid || waitpid(finfo->childpid, &pidstatus, WNOHANG)) 
    {
        if (is_forked)
        {
            dprintf("ERROR:  we should have child process %d, but appears to be dead\n", finfo->childpid);
            finfo->childpid=0;
            finfo->reader=-1;
            finfo->writer=-1;
            
            return NULL;
        }

        int child2par[2], par2child[2];

        /* our creation run.  create pipes and setup for fork */
        if (rp_pipe(child2par) == -1)
        {
            fprintf(stderr, "child2par pipe failed\n");
            return NULL;
        }

        if (rp_pipe(par2child) == -1)
        {
            fprintf(stderr, "par2child pipe failed\n");
            return NULL;
        }

        /* if child died, close old pipes */
        if (finfo->writer > 0)
        {
            close(finfo->writer);
            finfo->writer = -1;
        }
        if (finfo->reader > 0)
        {
            close(finfo->reader);
            finfo->reader = -1;
        }

        /***** fork ******/
        finfo->childpid = fork();
        if (finfo->childpid < 0)
        {
            fprintf(stderr, "fork failed");
            finfo->childpid = 0;
            return NULL;
        }

        if(finfo->childpid == 0)
        { /* child is forked once then talks over pipes. */
            char script[1024];
            setproctitle("rampart py_helper");

            is_child=1;

            close(child2par[0]);
            close(par2child[1]);

            // what a surprise, you can pass the pipe ints to the exec'd process, and it works.
            sprintf(script, scr_txt, par2child[0], child2par[1]);
            dprintf("executing %s\n", rampart_exec);

            execl(rampart_exec, rampart_exec, "-c", script, NULL);

            /*
            // while it would nicer to fork and then do_fork_loop(), python really really
            // doesn't like to be forked, hates being forked from a thread, and
            // especially despises being forked from a thread that didn't call Py_Initialize()
            finfo->writer = child2par[1];
            finfo->reader = par2child[0];

            libevent_global_shutdown();

            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);

            do_fork_loop(finfo); // loop and never come back;
            */
        }
        else
        {
            //parent
            pid_t *pidarg=NULL;

            set_forked; // we are only going to allow this once, unlike sql where we could fork again with new transaction

            signal(SIGPIPE, SIG_IGN); //macos
            signal(SIGCHLD, SIG_IGN);
            rp_pipe_close(child2par,1);
            rp_pipe_close(par2child,0);

            finfo->reader = child2par[0];
            finfo->writer = par2child[1];
            dprintf("reader=%d, writer=%d\n", finfo->reader, finfo->writer);
            fcntl(finfo->reader, F_SETFL, 0);

            // a callback to kill child
            REMALLOC(pidarg, sizeof(pid_t));
            *pidarg = finfo->childpid;
            set_thread_fin_cb(rpthread[threadno], py_kill_child, pidarg);
        }
    }

    return finfo;
}

/* ************** END PYTHON FORKING ************************ */


static duk_ret_t _p_to_string(duk_context *ctx)
{
    PyObject *pValue=NULL;
    PyGILState_STATE state;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pvalue"));
    pValue=(PyObject *)duk_get_pointer(ctx, -1);
    state = PYLOCK;
    push_ptype_to_string(ctx, pValue);
    PYUNLOCK(state);
    return 1;
}

static duk_ret_t _p_to_value(duk_context *ctx)
{
    PyObject *pValue=NULL;
    PyGILState_STATE state;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pvalue"));
    pValue=(PyObject *)duk_get_pointer(ctx, -1);

    duk_push_global_object(ctx);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pytojsmap")); 
    duk_pop(ctx);

    if(pValue)
    {
        state = PYLOCK;
        push_ptype(ctx, pValue);
        PYUNLOCK(state);
    }
    else
        duk_push_null(ctx);

    duk_push_global_object(ctx);
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pytojsmap"));
    duk_pop(ctx);

    return 1;
}

static duk_ret_t pvalue_finalizer(duk_context *ctx)
{
    PyObject *pValue=NULL;

    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("pvalue"));
    pValue=(PyObject *)duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    if(pValue)
    {
        PyGILState_STATE state = PYLOCK;
        RP_Py_XDECREF(pValue);
        PYUNLOCK(state);
    }
    else
    {
        duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("pref"));
        pValue=(PyObject *)duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        if(pValue)
            parent_finalizer(pValue);
    }
    return 0;
}

static void put_mod_funcs(duk_context *ctx, PyObject *pModule)
{
    PyObject *pobj = PyObject_Dir(pModule);

    dprintf("%schecking for functions in object. type: %s\n", pobj?"":"FAIL ", Py_TYPE(pModule)->tp_name);

    if(!pobj)
        return;

    Py_ssize_t len=PyList_Size(pobj), i=0;
    PyObject *value=NULL, *pFunc;

    while( i<len )
    {
        const char *fname;

        value=PyList_GetItem(pobj, i);
        fname = PyUnicode_AsUTF8(value);
        pFunc = PyObject_GetAttr(pModule, value);
        dprintf("checking %s - is %s - iscallable:%d\n", PyUnicode_AsUTF8(value), (pFunc? Py_TYPE(pFunc)->tp_name:"pFunc=NULL"), pFunc ? PyCallable_Check(pFunc):0);

        /* inherited funcs in a class come up as null (at least the first time) */
        if(!pFunc)
        {
            PyObject* pBase = (PyObject*) pModule->ob_type->tp_base;
            if(pBase)
            {
                pFunc = PyObject_GetAttr(pBase, value);
                dprintf("Got from base, pFunc=%p\n", pFunc);
            }
        }

        if(pFunc && PyCallable_Check(pFunc))
        {
            fname = PyUnicode_AsUTF8(value);

            duk_push_c_function(ctx, named_call, DUK_VARARGS);

            dprintf("%s is func\n", fname);
            // save our calling thread
            duk_push_int(ctx, get_thread_num() );
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("thrno"));

            // hidden pmod as property of function
            duk_push_pointer(ctx, (void*)pModule);
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pMod"));

            // name of this function
            duk_push_string(ctx, fname);
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pyfunc_name"));                

            // call is property of return object
            duk_put_prop_string(ctx, -2, fname);
        }
        i++;
        RP_Py_XDECREF(pFunc);
    }
}


static duk_ret_t py_call(duk_context *ctx)
{
    PyObject *pModule=NULL, *pValue=NULL, *pArgs=NULL, *pFunc=NULL, *kwdict=NULL;
    duk_idx_t i=1, top=duk_get_top(ctx);
    const char *err=NULL, *fname = REQUIRE_STRING(ctx, 0, "module.call: first argument must be a String");
    PyGILState_STATE state;
    int thrno, haskw=0;

    duk_push_current_function(ctx);

    // get our calling thread, make sure it matches importing thread
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("thrno"));
    thrno=duk_get_int(ctx, -1);
    duk_pop(ctx);
    if(thrno != get_thread_num() )
        RP_THROW(ctx, "Cannot execute python function from a module imported in a different thread");

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pMod"));
    pModule = (PyObject *) duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    if(pModule == NULL)
        RP_THROW(ctx, "No Module Found");

    if(!is_child && must_fork)
    {
        dprintf("in py_call, pmod=%p, ctx=%p\n", pModule, ctx);
        char *err=parent_py_call(pModule, fname);
        if(err)
            RP_THROW(ctx, err);
        return 1;
    }

    state = PYLOCK;
    pFunc = PyObject_GetAttrString(pModule, fname);

    if (!pFunc || !PyCallable_Check(pFunc))
    {
        err="error calling python function: %s";
        goto end;
    }
        
    // create the jsobj->pyobj map.  Used to detect and fix cyclical references
    duk_push_global_object(ctx);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("jstopymap")); 
    duk_pop(ctx);

    while(i<top)
    {
        if( duk_is_object(ctx, i) && duk_has_prop_string(ctx, i, "pyArgs") )
        {
            PyObject *val=NULL;
            const char *key;

            duk_get_prop_string(ctx, i, "pyArgs");
            kwdict=PyDict_New();
            duk_enum(ctx,-1,0);
            while (duk_next(ctx,-1,1))
            {
                key = duk_get_string(ctx, -2);
                if( duk_is_object(ctx,-1) && 
                    duk_has_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pvalue")) 
                )
                {
                    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pvalue"));
                    val=(PyObject *)duk_get_pointer(ctx, -1);
                    // pydict_setitem DOES NOT steal a reference. Go Figure. 
                    // so dont do: Py_XINCREF(pValue);
                    duk_pop(ctx);
                }
                else
                    val=type_to_pytype(ctx, -1);

                PyDict_SetItemString(kwdict, key, val);
                duk_pop_2(ctx);
            }
            duk_pop_2(ctx);//enum, pykeyword value

            haskw=1;
            duk_remove(ctx, i); // pykeyword object
            break;
        }
        i++;
    }

    i=1;
    top=duk_get_top(ctx);

    pArgs = PyTuple_New((int)top-1);
    //iterate through parameters, starting with the second.
    while (i<top)
    {
        // check if this is a variable returned from this function, with hidden pyobject in it.
        if( duk_is_object(ctx,i) && 
            duk_has_prop_string(ctx, i, DUK_HIDDEN_SYMBOL("pvalue")) 
        )
        {
            duk_get_prop_string(ctx, i, DUK_HIDDEN_SYMBOL("pvalue"));
            pValue=(PyObject *)duk_get_pointer(ctx, -1);
            // pytuple_setitem steals reference.  We have to increase to keep it
            Py_XINCREF(pValue);
            duk_pop(ctx);
        }
        else
            pValue=type_to_pytype(ctx, i);

        if(pValue==NULL)
        {
            err="error setting pValue: %s";
            goto end;
        }

        PyTuple_SetItem(pArgs, (int)i-1, pValue);
        i++;
    }

    if(haskw)
        pValue = Py_TYPE(pFunc)->tp_call(pFunc, pArgs, kwdict);
    else
        pValue = PyObject_CallObject(pFunc, pArgs);
    /*
    if(i>1)
        pValue = PyObject_CallMethod(pModule, fname, "O", pArgs);
    else
        pValue = PyObject_CallMethodNoArgs(pModule, PyUnicode_FromString(fname));
    */

    if(!pValue) {
        err="error calling python function: %s";
        goto end;
    }
    PYUNLOCK(state);

    duk_push_global_object(ctx);
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("jstopymap"));
    duk_pop(ctx);

    duk_push_object(ctx); //return value as object

    duk_push_pointer(ctx, pValue);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pvalue"));

    duk_push_c_function(ctx, pvalue_finalizer, 1);
    duk_set_finalizer(ctx, -2);
    
    duk_push_c_function(ctx, _p_to_string, 0);
    duk_put_prop_string(ctx, -2, "toString");

    duk_push_c_function(ctx, _p_to_value, 0);
    duk_put_prop_string(ctx, -2, "toValue");

    state=PYLOCK;

    put_mod_funcs(ctx, pValue);

    end:  //if goto, we never unlocked
    RP_Py_XDECREF(pFunc);
    RP_Py_XDECREF(pArgs);
    if(err)
    {
        char buf[MAX_EXCEPTION_LENGTH];
        char *exc = get_exception(buf);
        PYUNLOCK(state);
        RP_THROW(ctx, err, exc);
    }
    else if (PyErr_Occurred())
    {
        // well, we have something valid since pValue is not NULL.  And it might work.
        // example:
        //	   var pathlib = python.import('pathlib');
        //     var p = pathlib.Path("./");
        //   produces an error "'PosixPath' object has no attribute 'write_text'"
        //   but otherwise functions correctly.  So don't throw, but do log the error.
        // BTW: that example above no longer happens.  Seems that accessing any inherited members of a
        // class just once (as we do in put_mod_funcs() above) lets python access all its inherited members.
        char buf[MAX_EXCEPTION_LENGTH];
        char *exc = get_exception(buf);
        duk_push_sprintf(ctx, "Non-fatal error occured while executing '%s': %s", fname, &exc[1]);
        duk_put_prop_string(ctx, -2, "errMsg");
    }
    else
    {
        duk_push_string(ctx, "");
        duk_put_prop_string(ctx, -2, "errMsg");
    }
    PYUNLOCK(state);
    return 1;
}

static duk_ret_t named_call(duk_context *ctx)
{
    duk_push_current_function(ctx);
    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pyfunc_name")))
        RP_THROW(ctx, "Internal error getting python function name");
    duk_insert(ctx, 0);
    duk_pop(ctx); //current_func
    return py_call(ctx);
}

static duk_ret_t pmod_finalizer(duk_context * ctx)
{
    PyObject *pModule=NULL;


    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("pMod"));
    pModule=(PyObject *)duk_get_pointer(ctx, -1);

    if(!pModule)
        return 0;

    if(!is_child && must_fork)
    {
        //send message to child to release
        parent_finalizer(pModule);
        return 0;
    }

    PyGILState_STATE state=PYLOCK;
    RP_Py_XDECREF(pModule);
    PYUNLOCK(state);
    return 0;
}


duk_ret_t RP_PimportString (duk_context * ctx)
{
    PyObject *pCode=NULL, *pModule=NULL;
    PyGILState_STATE state;
    const char  *modname="module_from_string",
                *str = REQUIRE_STRING(ctx, 0, "python.importString: First argument must be a string (the module script)");
    if(!python_is_init) 
        rp_duk_python_init(ctx);

    if(duk_is_string(ctx,1))
        modname=duk_get_string(ctx,1);

    if(!is_child && must_fork)
    {
        char *name_script = strcatdup( strdup(modname), "\xff");
        char *fnames;

        name_script = strcatdup( name_script, (char*)str);

        pModule=parent_importString(name_script, &fnames);

        free(name_script);

        if(!pModule)
            RP_THROW(ctx, "error compiling python string: %s", duk_get_string(ctx, -1));

        duk_push_object(ctx);

        put_mod_funcs_from_string(ctx, pModule, fnames);
        free(fnames);

        duk_push_c_function(ctx, py_call, DUK_VARARGS);

        // save our calling thread
        duk_push_int(ctx, get_thread_num() );
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("thrno"));

        // hidden pmod as property of function
        duk_push_pointer(ctx, (void*)pModule);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pMod"));

        // call is property of return object
        duk_put_prop_string(ctx, -2, "call");

        // hidden pmod as property of object
        duk_push_pointer(ctx, (void*)pModule);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pMod"));

        duk_push_int(ctx, (int)get_thread_num() );
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("thrno"));

        dprintf("in RP_PimportString, pmod=%p, ctx=%p\n", pModule, ctx);
        return 1;
    }


    //dprintf("compile '%s', %s\n",str, modname);
    state = PYLOCK;

    pCode = Py_CompileString(str, modname, Py_file_input);

    if(!pCode)
    {
        char buf[MAX_EXCEPTION_LENGTH];
        char *exc = get_exception(buf);
        PYUNLOCK(state);
        RP_THROW(ctx, "error compiling python string: %s", exc);
    }
    pModule = PyImport_ExecCodeModule(modname, pCode );

    if(!pModule)
    {
        char buf[MAX_EXCEPTION_LENGTH];
        char *exc = get_exception(buf);
        PYUNLOCK(state);
        RP_THROW(ctx, "error executing python module: %s", exc);
    }

    duk_push_object(ctx);

    put_mod_funcs(ctx, pModule);

    PYUNLOCK(state);

    duk_push_c_function(ctx, py_call, DUK_VARARGS);

    // save our calling thread
    duk_push_int(ctx, get_thread_num() );
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("thrno"));

    // hidden pmod as property of function
    duk_push_pointer(ctx, (void*)pModule);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pMod"));

    // call is property of return object
    duk_put_prop_string(ctx, -2, "call");

    // hidden pmod as property of object
    duk_push_pointer(ctx, (void*)pModule);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pMod"));


    // finalizer for object
    duk_push_c_function(ctx, pmod_finalizer, 1);
    duk_set_finalizer(ctx, -2);

    return 1;
}

duk_ret_t RP_PimportFile (duk_context * ctx)
{
    const char *fname = REQUIRE_STRING(ctx, 0, "python.importFile - first argument must be a string (file name)");
    duk_push_true(ctx);
    duk_rp_read_file(ctx);
    duk_insert(ctx, 0);
    duk_push_string(ctx, fname);
    duk_insert(ctx, 1);
    return RP_PimportString(ctx);
}

duk_ret_t RP_Pimport (duk_context * ctx)
{
    const char *str = REQUIRE_STRING(ctx, 0, "python.import: First argument must be a string (filename)");
    PyObject *pModule;
    PyGILState_STATE state;

    if(!python_is_init)
        rp_duk_python_init(ctx);

    if(!is_child && must_fork)
    {
        char *fnames;

        pModule = parent_import(str, &fnames);
        if(!pModule)
        {
            RP_THROW(ctx,"error loading python module: %s", duk_get_string(ctx, -1) );
        }

        duk_push_object(ctx);

        put_mod_funcs_from_string(ctx, pModule, fnames);
        free(fnames);
        
    }
    else
    {
        state=PYLOCK;
        pModule = PyImport_ImportModule(str);
        if(!pModule)
        {
            char buf[MAX_EXCEPTION_LENGTH];
            char *exc = get_exception(buf);
            PYUNLOCK(state);
            RP_THROW(ctx, "error loading python module: %s", exc);
        }

        duk_push_object(ctx);

        put_mod_funcs(ctx, pModule);

        PYUNLOCK(state);
    }

    duk_push_c_function(ctx, py_call, DUK_VARARGS);

    // save our calling thread
    duk_push_int(ctx, get_thread_num() );
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("thrno"));

    // hidden pmod as property of function
    duk_push_pointer(ctx, (void*)pModule);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pMod"));

    // call is property of return object
    duk_put_prop_string(ctx, -2, "call");

    // hidden pmod as property of object
    duk_push_pointer(ctx, (void*)pModule);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pMod"));


    // finalizer for object
    duk_push_c_function(ctx, pmod_finalizer, 1);
    duk_set_finalizer(ctx, -2);

    return 1;
}


static duk_ret_t helper(duk_context *ctx)
{
    PFI *finfo=NULL;

    REMALLOC(finfo, sizeof(PFI));

    setproctitle("rampart py_helper");

    is_child=1;

    finfo->reader = REQUIRE_UINT(ctx,0, "Error, this function is meant to be run upon forking only");
    finfo->writer = REQUIRE_UINT(ctx,1, "Error, this function is meant to be run upon forking only");

    dprintf("reader=%d, writer=%d\n", finfo->reader, finfo->writer);

    finfo->childpid=0;
    finfo->ctx=ctx;
    parent_pid=getppid();

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    do_fork_loop(finfo); // loop and never come back;

    return 0;
}


/* **************************************************
   Initialize module
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
    rp_rpy_lock = RP_MINIT(&rpy_lock);

    duk_push_object(ctx);

    duk_push_c_function(ctx, RP_PimportString, 2);
    duk_put_prop_string(ctx, -2, "importString");

    duk_push_c_function(ctx, RP_Pimport, 1);
    duk_put_prop_string(ctx, -2, "import");

    duk_push_c_function(ctx, RP_PimportFile, 1);
    duk_put_prop_string(ctx, -2, "importFile");

    /* used when forking/execing */
    duk_push_c_function(ctx, helper, 2);
    duk_put_prop_string(ctx, -2, "__helper");

  return 1;
}

 