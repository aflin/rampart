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

#define xxdprintf(...) do{\
    printf("(%d) at %d (thread %d): ", (int)getpid(),__LINE__, get_thread_num());\
    printf(__VA_ARGS__);\
    fflush(stdout);\
}while(0)

#define pytostr(pv) ({\
    PyObject *ps=PyObject_Str((pv));\
    char *ret=strdup(PyUnicode_AsUTF8(ps));\
    Py_XDECREF(ps);\
    ret;\
})

#define rpydebug 0

#if rpydebug > 0

int debugl=rpydebug;
#define dprintf(level, ...) do{\
    if(level<=debugl){\
        if(is_child) printf("(%d) at %d (fork   %d): ", (int)getpid(),__LINE__, is_child);\
        else         printf("(%d) at %d (thread %d): ", (int)getpid(),__LINE__, get_thread_num());\
        printf(__VA_ARGS__);\
        fflush(stdout);\
    }\
}while(0)

#define dprintf_pyvar(level, _s, _v, ...) do{\
    if(level<=debugl){\
        char *_s = pytostr(_v);\
        if(is_child) printf("(%d) at %d (fork   %d): ", (int)getpid(),__LINE__, is_child);\
        else         printf("(%d) at %d (thread %d): ", (int)getpid(),__LINE__, get_thread_num());\
        printf(__VA_ARGS__);\
        free(_s);\
        fflush(stdout);\
    }\
}while(0)

#define dcheckvar(level, var) do{\
    if(level<=debugl){\
        if(is_child) printf("(%d) at %d (fork   %d): ", (int)getpid(),__LINE__, is_child);\
        else         printf("(%d) at %d (thread %d): ", (int)getpid(),__LINE__, get_thread_num());\
        printf("%s->refcnt = %d\n", #var, (int)Py_REFCNT((var)) );\
        fflush(stdout);\
    }\
}while(0)

#define dprintfhex(level,_h,_hsz,...) do{\
    if(level<=debugl){\
        if(is_child) printf("(%d) at %d (fork   %d): ", (int)getpid(),__LINE__, is_child);\
        else         printf("(%d) at %d (thread %d): ", (int)getpid(),__LINE__, get_thread_num());\
        printf(__VA_ARGS__);\
        printhex(_h,_hsz);putchar('\n');\
        fflush(stdout);\
    }\
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
        char *s=pytostr((r));\
        dprintf(5,"xdecref, refcnt was %d for %s\n",cnt, s);\
        free(s);\
        /* deallocated objects often return a random negative number*/\
        if(cnt<0) dprintf(4,"DANGER WILL ROBINSON!!! possible xdecref on deallocated python object\n");\
        if(cnt>0) Py_XDECREF((r));\
        if(!cnt) dprintf(4,"DANGER, xdecref on an item with refcnt=0\n");\
        r=NULL;\
    }\
    else  dprintf(4,"xdecref, ref was null\n");\
} while(0)

#define PYUNLOCK(s) do {\
    if(PyErr_Occurred()){\
        dprintf(1,"Unhandled exception");\
        PyErr_Clear();\
    }\
    if(!is_child) {\
        PyGILState_Release((s));\
        dprintf(5,"RELEASED pylock\n");\
    }\
}while (0)

#else

#define dprintf(...) /* nada */

#define dprintf_pyvar(...) /* nada */

#define dprintfhex(...) /* nada */

#define dcheckvar(...) /* nada */

#define RP_Py_XDECREF(r) do{\
    if(r) {Py_XDECREF((r));r=NULL;}\
} while(0)

#define PYUNLOCK(s) do {\
    PyErr_Clear();\
    if(!is_child) {\
        PyGILState_Release((s));\
        dprintf(5,"RELEASED pylock\n");\
    }\
}while (0)

#endif

#define PYLOCK ({\
    PyGILState_STATE _s={0};\
    if(!is_child) {\
        dprintf(5,"GETTING pylock\n");\
        /*if (!PyGILState_Check()) dprintf(4,"is already locked\n");*/\
        _s=PyGILState_Ensure();\
        dprintf(5,"GOT pylock\n");\
    }\
    _s;\
})

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


/* ********* Marshal-like pickle functions **********/

static PyObject * PyPickle=NULL;
static PyObject * pDumps=NULL;
static PyObject * pLoads=NULL;

static void init_pickle()
{
    if(!pDumps)
        pDumps = PyUnicode_FromString("dumps");
    if(!pLoads)
        pLoads = PyUnicode_FromString("loads");

    if(!PyPickle)
        PyPickle = PyImport_ImportModule("pickle");
}

static PyObject *PyPickle_ReadObjectFromString(const char *data, Py_ssize_t len)
{
    PyObject *pstr = PyBytes_FromStringAndSize(data, len);
    PyObject *ret=NULL;

    ret = PyObject_CallMethodOneArg(PyPickle, pLoads, pstr);
    RP_Py_XDECREF(pstr);

    return ret;
}

static PyObject *PyPickle_WriteObjectToString(PyObject *value, int version)
{
    //version currently ignored
    return PyObject_CallMethodOneArg(PyPickle, pDumps, value);
}


/* ********* END Marshal-like pickle functions **********/



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
                int i=0, total=0;
                
                // reverse the order to match duktape error ordering.
                while (pTrace != NULL)
                {
                    total++;
                    pTrace = pTrace->tb_next;
                }

                PyTracebackObject *rtrace[total];

                pTrace = traceRoot;
                while (pTrace != NULL)
                {
                    i++;
                    rtrace[total-i] = pTrace;
                    pTrace = pTrace->tb_next;
                }

                for (i=0; i<total;i++)
                {
                    pTrace = rtrace[i];

                    PyFrameObject* frame = pTrace->tb_frame;
                    PyCodeObject* code = PyFrame_GetCode(frame);
                    int lineNr = PyFrame_GetLineNumber(frame);
                    const char *sCodeName = PyUnicode_AsUTF8(code->co_name);
                    const char *sFileName = PyUnicode_AsUTF8(code->co_filename);

                    len = snprintf(s, left, "\n    at python:%s (%s:%d)", sCodeName, sFileName, lineNr);
                    left-=len;
                    s+=len;
                    if(len<0)
                        break;
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
static void push_ptype(duk_context *ctx, PyObject * pyvar);
static void start_pytojs(duk_context *ctx);

static PyObject * type_to_pytype(duk_context *ctx, duk_idx_t idx);
static void start_jstopy(duk_context *ctx);

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
PFI finfo_d;

static PFI *check_fork();

//

static int send_val(PFI* finfo, PyObject *pRef, char *err)
{
    PyObject *pBytes=NULL;
    PyGILState_STATE state;
    if(err)
    {
        size_t err_sz = strlen(err)+1;
        if(forkwrite("e", sizeof(char)) == -1)
            return 0;

        if(forkwrite(&err_sz, sizeof(size_t)) == -1)
            return 0;

        if(forkwrite(err, err_sz) == -1)
            return 0;
        return 1;
    }

    if(pRef == NULL)
    {
        if(forkwrite("n", sizeof(char)) == -1)
            return 0;
        return 1;
    }


    state=PYLOCK;
    pBytes=PyPickle_WriteObjectToString(pRef,Py_MARSHAL_VERSION);

    if(!pBytes)
    {

        PyErr_Clear(); //clear error from failed pickle

        // this object cannot be pickled.  We need to store it in this proc
        PyObject *pResStr = PyObject_Str(pRef);
        const char *str = PyUnicode_AsUTF8(pResStr);
        size_t str_sz = strlen(str) + 1;

        RP_Py_XDECREF(pResStr);

        PYUNLOCK(state);

        if(forkwrite("s", sizeof(char)) == -1)
            return 0;

        if(forkwrite(&str_sz, sizeof(size_t)) == -1)
            return 0;

        if(forkwrite(str, str_sz) == -1)
            return 0;
    }
    else
    {
        Py_ssize_t pickle_sz;
        char *pickle;

        dprintf(4,"extracting bytes from pBytes=%p\n", pBytes);
        PyBytes_AsStringAndSize( pBytes, &pickle, &pickle_sz );
        pickle_sz++;

        PYUNLOCK(state);

        if(forkwrite("m", sizeof(char)) == -1)
            return 0;

        if(forkwrite(&pickle_sz, sizeof(Py_ssize_t)) == -1)
            return 0;

        dprintf(4,"bytes sz=%d\n", (int)pickle_sz);
        if(forkwrite(pickle, pickle_sz) == -1)
            return 0;
    }
    return 1;
}

static PyObject *rp_trigger(PyObject *self, PyObject *args)
{
    RPTHR *thr = get_current_thread();
    duk_context *ctx = thr->ctx;
    const char *ev;
    PyObject *evarg=NULL;
    PFI *finfo=&finfo_d;

    if (!PyArg_ParseTuple(args, "s|O", &ev, &evarg))
        return NULL;

    /* if we are child process */
    if(is_child)
    {
        size_t str_sz = strlen(ev)+1;
        if(forkwrite("t", sizeof(char)) == -1)
        {
            fprintf(stderr,"rampart.triggerEvent: pipe error in child\n");
            exit(1);
        }
        if(forkwrite(&str_sz, sizeof(size_t)) == -1)
        {
            fprintf(stderr,"rampart.triggerEvent: pipe error in child\n");
            exit(1);
        }

        if(forkwrite(ev, str_sz) == -1)
        {
            fprintf(stderr,"rampart.triggerEvent: pipe error in child\n");
            exit(1);
        }

        if(!send_val(finfo, evarg, NULL))
        {
            fprintf(stderr,"rampart.triggerEvent: pipe error in child\n");
            exit(1);
        }

        return Py_None;
    }

    /* no fork */

    duk_push_c_function(ctx, duk_rp_trigger_event, 2); //rampart.event.trigger
    duk_push_string(ctx, ev); // event name
    if(evarg)
    {
        start_pytojs(ctx);
        push_ptype(ctx, evarg);   // translate python var to js var 
    }
    else
        duk_push_undefined(ctx);
    duk_call(ctx, 2);         // call rampart.event.trigger(ev,evarg)

    return Py_None;
}

PyObject *receive_pval(PFI *finfo, char **err)
{
    PyObject *pValue=Py_None;
    char type='X';
    char *str=NULL;
    size_t str_sz=0;

    /* get var type */
    if(forkread(&type, sizeof(char)) == -1)
        return NULL;

    dprintf(4, "got val from child, type %c\n",type);

    *err=NULL;
    if(type=='e')
    {
        if(forkread(&str_sz, sizeof(size_t)) == -1)
            return NULL;
        REMALLOC(str, (size_t)str_sz);

        if(forkread(str, str_sz) == -1)
        {
            free(str);
            return NULL;
        }

        *err = str;

        return NULL;
    }
    else if(type=='m')
    {
        char *pickle=NULL;
        Py_ssize_t pickle_sz=0;

        if(forkread(&pickle_sz, sizeof(Py_ssize_t)) == -1)
            return NULL;

        REMALLOC(pickle, (size_t)pickle_sz);

        if(forkread(pickle, pickle_sz) == -1)
        {
            free(pickle);
            return NULL;
        }

        pValue=PyPickle_ReadObjectFromString((const char*)pickle, pickle_sz);

        free(pickle);
    }
    else if (type=='s')
    {
        if(forkread(&str_sz, sizeof(size_t)) == -1)
            return NULL;

        REMALLOC(str, (size_t)str_sz);

        if(forkread(str, str_sz) == -1)
        {
            free(str);
            return NULL;
        }

        dprintf(4, "got string val from child, %s\n", str);

        pValue = PyUnicode_FromString(str);
        free(str);
    }
    else if (type !='n') // if anything but n or the above
    {
        fprintf(stderr,"rampart.call: pipe error in child\n");
        exit(1);
    }

    return pValue;
}

static PyObject *rp_call(PyObject *self, PyObject *args)
{
    RPTHR *thr = get_current_thread();
    duk_context *ctx = thr->ctx;
    const char *funcname;
    PyObject *pSlice=NULL, *pRet=Py_None;
    PFI *finfo=&finfo_d;
    Py_ssize_t tsize=0;
    int i=0;

    pSlice=PyTuple_GetSlice(args, 0, 1);

    if (!PyArg_ParseTuple(pSlice, "s", &funcname))
        return NULL;

    tsize = PyTuple_Size(args);

    /* if we are child process */
    if(is_child)
    {
        char *err=NULL;
        if(forkwrite("c", sizeof(char)) == -1)
        {
            fprintf(stderr,"rampart.call: pipe error in child\n");
            exit(1);
        }

        if(!send_val(finfo, args, NULL))
        {
            fprintf(stderr,"rampart.call: pipe error in child\n");
            exit(1);
        }

        pRet = receive_pval(finfo, &err);

        if(err)
        {
            PyErr_SetString(PyExc_RuntimeError, err);
            free(err);
            return NULL;
        }

        // if err and pret are NULL, this is a pipe error
        if(!pRet)
        {
            fprintf(stderr,"rampart.call: pipe error in child\n");
            exit(1);
        }

        return pRet;
    }

    /* no fork */

    if(!duk_get_global_string(ctx, funcname))
    {
        duk_pop(ctx);
        PyErr_SetString(PyExc_RuntimeError, "No such function in rampart's global scope");
        return NULL;
    }
    if(!duk_is_function(ctx, -1))
    {
        duk_pop(ctx);
        PyErr_SetString(PyExc_RuntimeError, "No such function in rampart's global scope");
        return NULL;
    }

    if(tsize>1)
    {
        duk_idx_t aidx = duk_get_top(ctx);
        int l=0;

        start_pytojs(ctx);
        push_ptype(ctx, args);   // translate python tuple to js array 
        l=(int)duk_get_length(ctx, -1);
        for (i=1;i<l;i++)
        {
            duk_get_prop_index(ctx, aidx, (duk_uarridx_t)i);
        }
        duk_remove(ctx,aidx);
    }
    else
        duk_push_undefined(ctx);

    duk_call(ctx, i-1);         // call rfunc(ev, ...)

    if(!duk_is_undefined(ctx, -1) && !duk_is_null(ctx, -1))
    {
        start_jstopy(ctx);
        pRet=type_to_pytype(ctx, -1);
    }

    duk_pop(ctx);
    return pRet;
}

// pretty much verbatim from https://docs.python.org/3/extending/extending.html#the-module-s-method-table-and-initialization-function
static PyMethodDef rampartMethods[] = {
    {"triggerEvent",  rp_trigger, METH_VARARGS, "Trigger a rampart event"},
    {"call",  rp_call, METH_VARARGS, "Call a global rampart function"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef rampartmodule = {
    PyModuleDef_HEAD_INIT,
    "rampart",   /* name of module */
    NULL, /* module documentation, may be NULL */
    -1,       /* size of per-interpreter state of the module,
                 or -1 if the module keeps state in global variables. */
    rampartMethods
};
PyMODINIT_FUNC PyInit_rampart(void)
{
    return PyModule_Create(&rampartmodule);
}
//

static void init_python(const char *program_name, char *ppath)
{

    PyGILState_STATE state;
    PyObject *paths = NULL;
    int maxp = 3*PATH_MAX+4;
    char tpath[maxp];

    //when building for yosemite and running on catalina, this is necessary (perhaps elsewhere too?)
    snprintf(tpath, maxp, "%s:%s/site-packages:%s/lib-dynload", ppath,ppath,ppath);
    setenv("PYTHONPATH", tpath, 0);
    setenv("PYTHONHOME", ppath, 0);

//
    if (PyImport_AppendInittab("rampart", PyInit_rampart) == -1) {
        fprintf(stderr, "Error: could not extend in-built modules table\n");
        exit(1);
    }
//
    Py_Initialize();

    state=PYLOCK;

    paths = PyList_New((Py_ssize_t) 4);
    PyList_SetItem(paths, (Py_ssize_t) 0, PyUnicode_FromString( "./"  ));
    PyList_SetItem(paths, (Py_ssize_t) 1, PyUnicode_FromString( ppath  ));
    snprintf(tpath, PATH_MAX, "%s/site-packages", ppath);
    PyList_SetItem(paths, (Py_ssize_t) 2, PyUnicode_FromString( tpath  ));
    snprintf(tpath, PATH_MAX, "%s/lib-dynload", ppath);
    PyList_SetItem(paths, (Py_ssize_t) 3, PyUnicode_FromString( tpath  ));

    PySys_SetObject("path", paths);

    init_pickle();

    PYUNLOCK(state);

    if(!is_child)
        (void)PyEval_SaveThread(); //so badly named, when what we want is to begin in an unlocked state.
}

static char ppath[PATH_MAX-15];  //room for /site-packages\0

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
        //why?    if (duk_is_function(ctx, -1) || duk_is_c_function(ctx, -1) )
        if (duk_is_function(ctx, -1))
                Py_RETURN_NONE;
            return obj_to_pytype(ctx, idx);
        case DUK_TYPE_UNDEFINED:
        case DUK_TYPE_NULL:
        default:
            Py_RETURN_NONE;
    }
}

static void start_jstopy(duk_context *ctx)
{
    duk_push_global_object(ctx);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("jstopymap")); 
    duk_pop(ctx);
}

/* ************** END JSVAR -> PYTHON VAR ******************* */

/* ****************** PYTHON VAR -> JSVAR ******************* */


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
        //keys in dicts can be anything, so extract it as a string
        push_ptype_to_string(ctx, key);
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
                char *s = get_exception(buf);
                RP_THROW(ctx, "error converting return python integer:\n %s",(s+1));
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
static void start_pytojs(duk_context *ctx)
{
    duk_push_global_object(ctx);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pytojsmap")); 
    duk_pop(ctx);
}

/* ************** END PYTHON VAR -> JSVAR ******************* */

/* ************** PYTHON FORKING **************************** */




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
    dprintf(4,"killed child %d\n",(int)*kpid);
    clear_forked; // kinda pointless now, since this is not called unless thread dies
                  // when all flags are cleared.  May be needed later.
}

static duk_ret_t named_call(duk_context *ctx);
static duk_ret_t _p_to_string(duk_context *ctx);
static duk_ret_t _p_to_value(duk_context *ctx);
static duk_ret_t pvalue_finalizer(duk_context *ctx);
static duk_ret_t py_call(duk_context *ctx);
static void put_attributes(duk_context *ctx, PyObject *pValue);
static void make_proxy(duk_context *ctx);


static void parent_finalizer(PyObject *pModule)
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

static int child_finalizer(PFI *finfo)
{
    PyObject *pModule=NULL;

    if(forkread(&pModule, sizeof(PyObject *)) == -1)
        return 0;

    RP_Py_XDECREF(pModule);
    dprintf(4,"Finalized in child\n");
    if(forkwrite("o", sizeof(char)) == -1)
        return 0;

    return 1;    
}

static duk_ret_t _get_pref_val(duk_context *ctx)
{
    PyObject *pRef=NULL, *pValue=NULL;
    PFI *finfo=check_fork();
    char type='X';

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pref"));
    pRef=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(!pRef)
        RP_THROW(ctx, "internal error getting value");

    if(forkwrite("v", sizeof(char)) == -1)
        RP_THROW(ctx, "internal error getting value");

    if(forkwrite(&pRef, sizeof(PyObject *)) == -1)
        RP_THROW(ctx, "internal error getting value");


    if(forkread(&type, sizeof(char)) == -1)
        RP_THROW(ctx, "internal error getting value");

    dprintf(4, "got val from child, type %c\n",type);
    if(type=='m')
    {
        char *pickle=NULL;
        Py_ssize_t pickle_sz=0;
        PyGILState_STATE state;

        if(forkread(&pickle_sz, sizeof(Py_ssize_t)) == -1)
            RP_THROW(ctx, "internal error getting value");

        REMALLOC(pickle, (size_t)pickle_sz);

        if(forkread(pickle, pickle_sz) == -1)
        {
            free(pickle);
            RP_THROW(ctx, "internal error getting value");
        }

        start_pytojs(ctx);
        state = PYLOCK;

        pValue=PyPickle_ReadObjectFromString((const char*)pickle, pickle_sz);
        push_ptype(ctx, pValue);
        dprintf_pyvar(4, x, pValue, "got pickle val from child, %s\n", x);
        PYUNLOCK(state);

        free(pickle);
    }
    else if (type=='s')
    {
        char *str=NULL;
        size_t str_sz=0;

        if(forkread(&str_sz, sizeof(size_t)) == -1)
            RP_THROW(ctx, "internal error getting value");

        REMALLOC(str, (size_t)str_sz);

        if(forkread(str, str_sz) == -1)
        {
            free(str);
            RP_THROW(ctx, "internal error getting value");
        }

        dprintf(4, "got string val from child, %s\n", str);

        duk_push_string(ctx, (const char *)str);
        free(str);
    }
    else
        RP_THROW(ctx, "internal error getting value");

    return 1;
}

static duk_ret_t _get_pref_str(duk_context *ctx)
{
//    duk_push_this(ctx);
    duk_push_current_function(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("prefstr"));
    return 1;
}

static void put_func_attributes(duk_context *ctx, PyObject *pValue, PyObject *pRef, const char *pstring)
{
    PyObject *pStr=NULL;
    PyGILState_STATE state;

    // save our calling thread
    duk_push_int(ctx, get_thread_num() );
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("thrno"));

    // hidden pmod as property of function
    if(pValue)
    {
        duk_push_pointer(ctx, (void*)pValue);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pvalue"));
    }

    if(pRef)
    {
        duk_push_pointer(ctx, (void*)pRef);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pref"));
    }

    state=PYLOCK;

    if(pValue && !pstring)
    {
        pStr=PyObject_Str(pValue);
        pstring = PyUnicode_AsUTF8(pStr);
    }

    if(pstring)
    {
        duk_push_c_function(ctx, _get_pref_str, 0);
        duk_push_string(ctx, pstring);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("prefstr"));
        duk_put_prop_string(ctx, -2, "toString");

        duk_push_c_function(ctx, _get_pref_str, 0);
        duk_push_string(ctx, pstring);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("prefstr"));
        duk_put_prop_string(ctx, -2, "toValue");

        duk_push_c_function(ctx, _get_pref_str, 0);
        duk_push_string(ctx, pstring);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("prefstr"));
        duk_put_prop_string(ctx, -2, "valueOf");

        RP_Py_XDECREF(pStr);
    }
    PYUNLOCK(state);
}

static void push_python_function_as_method(duk_context *ctx, const char *fname, PyObject *pValue, char *refstr)
{
    duk_push_c_function(ctx, named_call, DUK_VARARGS);

    dprintf(4,"%s is func\n", fname);

    if(!is_child && must_fork)
        put_func_attributes(ctx, NULL, pValue, refstr);
    else
        put_func_attributes(ctx, pValue, NULL, refstr);

    // name of this function
    duk_push_string(ctx, fname);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pyfunc_name"));                
}


/* turn a python var into an object which we can use in js */
#define make_pyval(pValue, strfunc, valfunc, fn, pRef, refstr) do{\
    PyGILState_STATE state;\
    char *funcnames="";\
\
    if(fn) funcnames=(char*)(fn);\
\
    duk_push_object(ctx); /*return value as object */\
\
    if(pValue){\
        duk_push_pointer(ctx, pValue);\
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pvalue"));\
    }\
\
    duk_push_c_function(ctx, pvalue_finalizer, 1);\
    duk_set_finalizer(ctx, -2);\
    \
    if(refstr) {/*push if null pointer also*/\
        duk_push_pointer(ctx, pRef);\
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pref"));\
    }\
    duk_push_c_function(ctx, strfunc, 0);\
    if(refstr){\
        dprintf(4,"putting refstr %s on strfunc\n", (char*)refstr);\
        duk_push_string(ctx, refstr);\
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("prefstr"));\
    }\
    duk_put_prop_string(ctx, -2, "toString");\
\
    duk_push_c_function(ctx, valfunc, 0);\
    duk_put_prop_string(ctx, -2, "toValue");\
\
    duk_push_c_function(ctx, valfunc, 0);\
    duk_put_prop_string(ctx, -2, "valueOf");\
\
    duk_push_int(ctx, (int)get_thread_num() );\
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("thrno"));\
\
    state=PYLOCK;\
    if(pRef){\
        if(strlen(funcnames))\
            put_attributes_from_string(ctx, pRef, funcnames);\
    } else {\
        dprintf_pyvar(4, x, pValue, "putting mod funcs for %s\n",x);\
        put_attributes(ctx, pValue);\
    }\
\
    PYUNLOCK(state);\
\
    make_proxy(ctx); /* turn return object into a proxy so we can look up more keys */\
} while(0)


/* here *pModule is invalid in this process.  Use strings sent from child */
static void put_attributes_from_string(duk_context *ctx, PyObject *pModule, char *s)
{
    char *end=NULL;
    char *spe, *spf, *refstr;

    while( 1 )
    {

        spf=strchr(s, '\xff');
        spe=strchr(s, '\xfe');

        if(!spe && !spf)
        {
            break;
        }

        if(!spe || (spf && spf<spe) )
        {
            // format is ("%s\xff%p\xff%s\xff", name, pointer, pytoval(pointer) )
            PyObject *pRef=NULL;

            //terminate s (the name)
            *spf='\0';
            //advance to pointer
            spf++;
            //terminate pointer
            refstr=strchr(spf, '\xff');
            *refstr='\0';
            // scan pointer
            sscanf(spf,"%p", &pRef);
            //advance to refstr
            refstr++;
            //terminate refstr
            end=strchr(refstr, '\xff');
            if(end)
                *end='\0';
            // make the value

            make_pyval(NULL, _get_pref_str, _get_pref_val, NULL, pRef, refstr);
            duk_put_prop_string(ctx, -2, s);
        }
        else if(!spf || (spe && spe<spf))
        {
            // format is ("%s\xfe%s\xfe", name, pytoval(pointer) )
            //terminate s (the name)
            *spe='\0';
            //advance to refstr
            refstr = spe+1;
            //terminate refstr
            end=strchr(refstr, '\xfe');
            if(end)
                *end='\0';

            // make the method call
            push_python_function_as_method(ctx, s, pModule, refstr);
            duk_put_prop_string(ctx, -2, s);
        }

        s=end+1; //advance to next entry

        if(*s=='\0') //if the last one
            break;
    }
}

static int receive_val_and_push(duk_context *ctx, PFI *finfo)
{
    PyObject *pValue=NULL;
    char type='X';
    char *str=NULL;
    size_t str_sz=0;

    /* get var type */
    if(forkread(&type, sizeof(char)) == -1)
        return 0;

    dprintf(4, "got val from child, type %c\n",type);

    if(type=='n')
        duk_push_undefined(ctx);
    else if(type=='m')
    {
        char *pickle=NULL;
        Py_ssize_t pickle_sz=0;
        PyGILState_STATE state;

        if(forkread(&pickle_sz, sizeof(Py_ssize_t)) == -1)
            return 0;

        REMALLOC(pickle, (size_t)pickle_sz);

        if(forkread(pickle, pickle_sz) == -1)
        {
            free(pickle);
            return 0;
        }

        start_pytojs(ctx);
        state = PYLOCK;

        pValue=PyPickle_ReadObjectFromString((const char*)pickle, pickle_sz);
        push_ptype(ctx, pValue);
        dprintf_pyvar(4, x, pValue, "got pickle val from child, %s\n", x);
        PYUNLOCK(state);

        free(pickle);
    }
    else if (type=='s')
    {
        if(forkread(&str_sz, sizeof(size_t)) == -1)
            return 0;

        REMALLOC(str, (size_t)str_sz);

        if(forkread(str, str_sz) == -1)
        {
            free(str);
            return 0;
        }

        dprintf(4, "got string val from child, %s\n", str);

        duk_push_string(ctx, (const char *)str);
        free(str);
    }
    else
        return 0;

    return 1;
}

// parent version of rampart.triggerEvent in python
static void do_trigger(duk_context *ctx, PFI *finfo)
{
    char *ev=NULL;
    size_t str_sz=0;


    duk_push_c_function(ctx, duk_rp_trigger_event, 2); //rampart.event.trigger

    /* get event name */
    if(forkread(&str_sz, sizeof(size_t)) == -1)
        RP_THROW(ctx, "internal error getting value");

    REMALLOC(ev, (size_t)str_sz);

    if(forkread(ev, str_sz) == -1)
    {
        free(ev);
        RP_THROW(ctx, "internal error getting value");
    }

    duk_push_string(ctx, ev); // event name
    free(ev);

    if(!receive_val_and_push(ctx,finfo))
        RP_THROW(ctx, "python: rampart.call - internal error getting value");

    duk_call(ctx, 2);         // call rampart.event.trigger(ev,evarg)
}

// parent version of rampart.call in python
static int do_call(duk_context *ctx, PFI *finfo)
{
    const char *fname;
    int i=1, asize=0;
    duk_idx_t top=duk_get_top(ctx), aidx;
    PyObject *pArgs=NULL;
    char *err = NULL;

    if(!receive_val_and_push(ctx,finfo))
        RP_THROW(ctx, "python: rampart.call - internal error getting value");

    aidx=duk_get_top_index(ctx);

    asize = (int)duk_get_length(ctx, -1);
    if(!asize)
        RP_THROW(ctx, "python: rampart.call - internal error getting value");

    duk_get_prop_index(ctx, -1, 0);
    if(!duk_is_string(ctx, -1))
        RP_THROW(ctx, "python: rampart.call - internal error getting value");

    fname = duk_get_string(ctx, -1);
    duk_pop(ctx);

/*
    if(!duk_get_global_string(ctx, fname))
    {
        duk_set_top(ctx, top);
        err="rampart.call(): No such function in rampart's global scope";
    }
    if(!duk_is_function(ctx, -1))
    {
        duk_set_top(ctx, top);
        err="rampart.call(): No such function in rampart's global scope";
    }
*/
    duk_push_string(ctx, fname);
    if(duk_peval(ctx))
        err="rampart.call(\"%s\", ...): No such function in rampart's global scope";
    else
    if(!duk_is_function(ctx, -1))
        err="rampart.call(\"%s\", ...): No such function in rampart's global scope";

    if(err)
    {
        char tbuf[1024];
        snprintf(tbuf, 1024, err, fname);
        if(!send_val(finfo, NULL, tbuf))
        {
            fprintf(stderr,"pipe error\n");
            exit(1);
        }
        duk_set_top(ctx, top);

        return 1;
    }

    if(asize>1)
    {
        for (i=1;i<asize;i++)
        {
            duk_get_prop_index(ctx, aidx, (duk_uarridx_t)i);
        }
        duk_remove(ctx,aidx);
    }
    else
        duk_push_undefined(ctx);

    duk_call(ctx, asize-1);

    if (!duk_is_undefined(ctx, -1))
    {
        PyGILState_STATE state;

        state=PYLOCK;

        start_jstopy(ctx);
        pArgs=type_to_pytype(ctx, -1);

        PYUNLOCK(state);

        if(!send_val(finfo, pArgs, NULL))
        {
            fprintf(stderr,"pipe error\n");
            exit(1);
        }
        
        state=PYLOCK;
        RP_Py_XDECREF(pArgs);
        PYUNLOCK(state);
        return 1;
    }

    if(!send_val(finfo, pArgs, NULL))
    {
        fprintf(stderr,"pipe error\n");
        exit(1);
    }

    duk_set_top(ctx, top);

    return 1;
}

static PyObject *parent_import(duk_context *ctx, const char *script, int typeno, char **fnames, char **fstring)
{
    PFI *finfo = check_fork();
    size_t script_sz = strlen(script)+1; //include the null
    PyObject *pModule=NULL;
    char status='\0';
    char type='X', *stype = (typeno ? "s": "i");

    if(!finfo)
        return NULL;

    // stype is "s" or "i".  See do_fork loop below
    if(forkwrite(stype, sizeof(char)) == -1)
        return NULL;

    if(forkwrite(&script_sz, sizeof(size_t)) == -1)
        return NULL;

    if(forkwrite(script, script_sz) == -1)
        return NULL;

    goagain:
    if(forkread(&type, sizeof(char)) == -1)
        return NULL;

    // we are waiting for import to complete, however
    // if we get a 't' or a 'c', we got a request for rampart.triggerEvent or 
    // rampart.call somewhere in the main script.  Process request and loop back
    // to wait for python to finish importing.
    if(type == 't')
    {
        do_trigger(ctx, finfo);
        goto goagain;
    }
    else if(type == 'c')
    {
        if(!do_call(ctx, finfo))
            RP_THROW(ctx, "python: rampart.call - internal error getting value");
        goto goagain;
    }
    else if(type != 'M')
        return NULL;

    if(forkread(&pModule, sizeof(PyObject *)) == -1)
        return NULL;

    dprintf(4,"in parent got pmod=%p\n",pModule);

    if(forkread(&status, sizeof(char)) == -1)
        return NULL;

    if(status == 'o')
    {
        char *funcnames=NULL;
        char *funcstring=NULL;
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

        if(forkread(&flen, sizeof(size_t)) == -1)
            return NULL;

        REMALLOC(funcstring, flen);
        if(forkread(funcstring, flen) == -1)
        {
            free(funcstring);
            return NULL;
        }
        *fstring = funcstring;
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
    else
        duk_push_string(finfo->ctx, "Unknown error importing from child process");

    return NULL;
}

static char *stringify_funcnames(PyObject* pModule)
{
    PyObject *pobj = PyObject_Dir(pModule);
    int parent_is_callable = PyCallable_Check(pModule);
    char scratch[1024];

    dprintf(4,"%schecking for functions in object. type: %s\n", pobj?"":"FAIL ", Py_TYPE(pModule)->tp_name);

    if(!pobj)
    {
        char buf[MAX_EXCEPTION_LENGTH];
        const char *exc = get_exception(buf);
        dprintf(4,"pyobject_dir exception: %s\n", exc);        
        (void)exc;
        return strdup("");
    }

    Py_ssize_t len = PyList_Size(pobj), i=0;
    PyObject *value=NULL, *pFunc;
    char *str=strdup("");

    while( i<len )
    {
        size_t l;
        const char *fname;

        value=PyList_GetItem(pobj, i);
        fname = PyUnicode_AsUTF8(value);
        l=strlen(fname);

        if(l>3 && *fname=='_' && fname[1]=='_' && fname[l-2]=='_' && fname[l-1]=='_')
        {
            i++;
            continue;        
        }

        pFunc = PyObject_GetAttr(pModule, value);

        /* inherited funcs in a class come up as null (at least the first time) */
        dprintf(5,"checking %s - is %s - iscallable:%d\n", PyUnicode_AsUTF8(value), (pFunc? Py_TYPE(pFunc)->tp_name:"pFunc=NULL"), pFunc ? PyCallable_Check(pFunc):0);
        if(!pFunc)
        {
            PyObject* pBase = (PyObject*) pModule->ob_type->tp_base;
            if(pBase)
            {
                pFunc = PyObject_GetAttr(pBase, value);
                dprintf(4,"Got from base, pFunc=%p\n", pFunc);
            }
        }
        if(pFunc)
        {
            const char *pvs;
            PyObject *pStr;

            pStr=PyObject_Str(pFunc);
            pvs = PyUnicode_AsUTF8(pStr);
            if(PyCallable_Check(pFunc))
            {
                dprintf(4,"storing %s - is %s - iscallable:%d\n", PyUnicode_AsUTF8(value), (pFunc? Py_TYPE(pFunc)->tp_name:"pFunc=NULL"), pFunc ? PyCallable_Check(pFunc):0);
                str = strcatdup(str, (char *)fname);
                snprintf(scratch,1024,"\xfe%s", pvs);
                str = strcatdup(str, scratch);
                str = strcatdup(str, "\xfe");
            }
            else if (parent_is_callable)
            {
                str = strcatdup(str, (char *)fname);
                sprintf(scratch,"\xff%p",pFunc); 
                str = strcatdup(str, scratch);
                snprintf(scratch,1024,"\xff%s", pvs);
                str = strcatdup(str, scratch);
                str = strcatdup(str, "\xff");
            }
            RP_Py_XDECREF(pStr);
        }
        i++;
        RP_Py_XDECREF(pFunc);
    }
    PyErr_Clear();
    return str;
}

/* parent_import is above */
/* type 0=import, 1=importString */
static int child_import(PFI *finfo, int type)
{
    size_t script_sz;
    char *script=NULL;
    const char *exc=NULL;
    PyObject *pModule=NULL, *pCode=NULL;
    char *modname="module_from_string",
         *scriptname="*.js",
         *s,
         *freeme=NULL;
    char buf[MAX_EXCEPTION_LENGTH];

    dprintf(4,"in child_import\n");
    if (forkread(&script_sz, sizeof(size_t))  == -1)
    {
        dprintf(4,"fail read script_sz");
        return 0;
    }

    REMALLOC(script, script_sz);
    freeme=script;

    if (forkread(script, script_sz)  == -1)
    {
        dprintf(4,"fail read script");
        return 0;
    }

    if(type)
    {
        if( (s=strchr(script, '\xff')) )
        {
            modname=script;
            script=s+1;
            *s='\0';
            if( (s=strchr(script, '\xff')) )
            {
                scriptname=script;
                script=s+1;
                *s='\0';
            }
        }
    }

    dprintf(4,"in child_import - python_is_init=%d\n",python_is_init);
    if(!python_is_init)
        rp_duk_python_init(finfo->ctx);

    if(type)
    {
        pCode = Py_CompileString(script, scriptname, Py_file_input);
        dprintf(4,"in _child_import - pCode = %p\n", pCode);

        if(!pCode)
        {
            exc = get_exception(buf);
            dprintf(4,"_child_import: !pCode: %s, script:%s\n%s\n",exc, scriptname, script);
        }
        else
        {
            dprintf(4,"in _child_import -PyImport_ExecCodeModule(%s, %p)\n",modname, pCode);
            pModule = PyImport_ExecCodeModule(modname, pCode );
            dprintf(4,"in _child_import - pModule = %p\n", pModule);
        }
    }
    else
        pModule = PyImport_ImportModule(script);

    if(!pModule && !exc)
        exc = get_exception(buf);

    free(freeme);

    if(forkwrite("M", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&pModule, sizeof(PyObject*)) == -1)
        return 0;

    if(!exc)
    {
        char *funcnames = stringify_funcnames(pModule);
        size_t flen = strlen(funcnames)+1;
        PyObject *pResStr = PyObject_Str(pModule);
        const char *funcstring = PyUnicode_AsUTF8(pResStr);

        if(forkwrite("o", sizeof(char)) == -1)
            return 0;

        if(forkwrite(&flen, sizeof(size_t)) == -1)
            return 0;
        if(forkwrite(funcnames, flen) == -1)
            return 0;
        free(funcnames);

        flen = strlen(funcstring) + 1;
        if(forkwrite(&flen, sizeof(size_t)) == -1)
            return 0;
        if(forkwrite(funcstring, flen) == -1)
            return 0;

        RP_Py_XDECREF(pResStr);
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
    PyObject *pbytes=PyPickle_WriteObjectToString((obj),Py_MARSHAL_VERSION);\
    PyBytes_AsStringAndSize( pbytes, &_res, &(sz) );\
    (sz)++;\
    RP_Py_XDECREF(pbytes);\
    _res;\
})

int child_get_val(PFI* finfo)
{
    PyObject *pRef=NULL, *pBytes=NULL;

    if(forkread(&pRef, sizeof(PyObject *)) == -1)
        return 0;

    dprintf(4,"translating pRef=%p to pickle bytes\n", pRef);
    pBytes=PyPickle_WriteObjectToString(pRef,Py_MARSHAL_VERSION);

    if(!pBytes)
    {

        PyErr_Clear(); //clear error from failed pickle

        // this object cannot be pickled.  We need to store it in this proc
        PyObject *pResStr = PyObject_Str(pRef);
        const char *str = PyUnicode_AsUTF8(pResStr);
        size_t str_sz = strlen(str) + 1;

        if(forkwrite("s", sizeof(char)) == -1)
            return 0;

        if(forkwrite(&str_sz, sizeof(size_t)) == -1)
            return 0;

        if(forkwrite(str, str_sz) == -1)
            return 0;

        RP_Py_XDECREF(pResStr);
    }
    else
    {
        Py_ssize_t pickle_sz;
        char *pickle;

        dprintf(4,"extracting bytes from pBytes=%p\n", pBytes);
        PyBytes_AsStringAndSize( pBytes, &pickle, &pickle_sz );
        pickle_sz++;

        if(forkwrite("m", sizeof(char)) == -1)
            return 0;

        if(forkwrite(&pickle_sz, sizeof(Py_ssize_t)) == -1)
            return 0;

        dprintf(4,"bytes sz=%d\n", (int)pickle_sz);
        if(forkwrite(pickle, pickle_sz) == -1)
            return 0;
    }
    return 1;
}


static char *parent_py_call_read_error(PFI *finfo)
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

static char *parent_read_val(PFI *finfo)
{
    char retval='X';
    char *errmsg=NULL;
    duk_context *ctx = finfo->ctx;
    char *pipe_error="pipe error";

    goagain:
    if(forkread(&retval, sizeof(char)) == -1)
        return strdup(pipe_error);

    // we are waiting for function to end, however
    // if we get a 't' or a 'c', we got a request for rampart.triggerEvent or 
    // rampart.call somewhere in the function.  Process request and loop back
    // to wait for python function to return
    if(retval == 't')
    {
        do_trigger(ctx, finfo);
        goto goagain;
    }
    else if(retval == 'c')
    {
        if(!do_call(ctx, finfo))
            RP_THROW(ctx, "python: rampart.call - internal error getting value");
        goto goagain;
    }

    // python function returned something:
    else if(retval == 'e') // returned an error
    {
        errmsg=parent_py_call_read_error(finfo);

        if(!errmsg)
            return strdup(pipe_error);

        // RP_THROW with a free in the middle
        duk_push_error_object(ctx, DUK_ERR_SYNTAX_ERROR, "%s", errmsg);
        free(errmsg);
        (void) duk_throw(ctx);
    }
    else if(retval == 'r') // returned a ref, get reference to var, var stays in child
    {
        PyObject *pRef=NULL;
        size_t sz=0;
        char *refstr=NULL;
        int is_func=0;
        char *funcnames=NULL;
        size_t flen=0;

        if(forkread(&pRef, sizeof(duk_size_t)) == -1)
            return strdup(pipe_error);

        if(forkread(&sz, sizeof(duk_size_t)) == -1)
            return strdup(pipe_error);

        REMALLOC(refstr, sz);

        dprintf(4,"receiving refstring, sz=%d\n", (int)sz);

        if(forkread(refstr, sz) == -1)
            return strdup(pipe_error);

        dprintf(4,"refstring=%s\n", refstr);
        dprintf(4,"STORING pRef=%p in JS object\n", pRef);

        if(forkread(&flen, sizeof(size_t)) == -1)
            return strdup(pipe_error);

        if(flen)
        {
            REMALLOC(funcnames, flen);
            if(forkread(funcnames, flen) == -1)
            {
                free(funcnames);
                return strdup(pipe_error);
            }        
        }
        
        if(forkread(&is_func, sizeof(int)) == -1)
        {
            free(funcnames);
            return strdup(pipe_error);
        }

        if(!pRef)
        {
            errmsg=parent_py_call_read_error(finfo);
            if(!errmsg)
                return strdup(pipe_error);
            if(strlen(errmsg))
                duk_push_string(ctx, errmsg);
            else
                duk_push_undefined(ctx);

            free(funcnames);
            free(refstr);
            free(errmsg);
            errmsg=NULL;
            return errmsg;
        }
        else if(is_func)
        {
            duk_push_c_function(ctx, py_call, DUK_VARARGS);

            if(funcnames && strlen(funcnames))
            {
                put_attributes_from_string(ctx, pRef, funcnames);
            }

            put_func_attributes(ctx, NULL, pRef, refstr);
        }
        else
        {
            make_pyval(NULL, _get_pref_str, _get_pref_val, funcnames, pRef, refstr);
        }

        free(refstr);
        free(funcnames);

        errmsg=parent_py_call_read_error(finfo);
        if(!errmsg)
            return strdup(pipe_error);
        duk_push_string(ctx, errmsg);
        free(errmsg);
        errmsg=NULL;
        duk_put_prop_string(ctx, -2, "errMsg");
    }
    else
    {
        dprintf(4,"in parent_read_val, got bad response '%c' 0x%2x\n", retval, retval);
        //putc(retval,stderr); while (forkread(&retval, sizeof(char)) != -1) putc(retval,stderr);
        return strdup("error: bad response from python child process");
    }
    return errmsg;

}
static char *parent_py_call(PyObject * pModule, const char *fname)
{
    PFI *finfo = check_fork();
    PyObject *pArgs=NULL, *pValue=NULL, *pBytes=NULL;
    duk_context *ctx;
    duk_idx_t i=1, top;
    char *pickle=NULL;
    Py_ssize_t pickle_sz=0;
    size_t fname_sz = 0;
    char *pipe_error="pipe error";
    PyGILState_STATE state;

    if(!finfo)
        return strdup("error retrieving fork information.");

    if(fname) //method call
        fname_sz = strlen(fname)+1;


    ctx=finfo->ctx;
    top=duk_get_top(ctx);

    dprintf(4,"parent_py_call, top=%d\n", (int)top);

    if( i<top) //we have arguments
    {
        PyObject **pPtrs = NULL, *kwdict=NULL;
        int npPtrs=0;

        state = PYLOCK;

        kwdict=PyDict_New(); //this holds named parameters/keyword args to function

        // create the jsobj->pyobj map.  Used to detect and fix cyclical references
        start_jstopy(ctx);

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
                        //store it as a long in a dictionary so it is pickleable
                        void *refptr = NULL;

                        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pref"));
                        refptr = duk_get_pointer(ctx, -1);
                        duk_pop(ctx);

                        REMALLOC(pPtrs, sizeof(PyObject *) * (npPtrs + 1));
                        pPtrs[npPtrs] = PyLong_FromVoidPtr(refptr);
                        val = PyDict_New();
                        PyDict_SetItemString(val, "___REF_IN_CHILD___", pPtrs[npPtrs]);
                        dprintf(4,"PUT pRef from storage in PyObj, sending pRef=%p\n", refptr);
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

        start_jstopy(ctx);
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
                dprintf(4,"PUT pRef from storage in PyObj, sending pRef=%p\n", refptr);
                npPtrs++;
            }
            else
                pValue=type_to_pytype(ctx, i);

            if(pValue==NULL)
            {
                PYUNLOCK(state);
                return strdup("error setting pValue");
            }

            PyTuple_SetItem(pArgs, (int)i-1, pValue);
            i++;
        }

        //add the kwdict at the end of tuple
        PyTuple_SetItem(pArgs, (int)i-1, kwdict);

        dprintf(4,"pArgs = %p, %s\n", pArgs, PyUnicode_AsUTF8(PyObject_Str(pArgs)) );

        pBytes=PyPickle_WriteObjectToString(pArgs,Py_MARSHAL_VERSION);
        if(!pBytes)
        {
            char buf[MAX_EXCEPTION_LENGTH];
            fprintf(stderr, "exception: %s\n", get_exception(buf));
            abort();
        }
        PyBytes_AsStringAndSize( pBytes, &pickle, &pickle_sz );
        pickle_sz++;

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
        return strdup(pipe_error);

    if(forkwrite(&pModule, sizeof(PyObject *)) == -1)
        return strdup(pipe_error);

    if(forkwrite(&fname_sz, sizeof(size_t)) == -1)
        return strdup(pipe_error);

    if( fname_sz ) //method call
    {
        if(forkwrite(fname, fname_sz) == -1)
            return strdup(pipe_error);
    }

    if(forkwrite(&pickle_sz, sizeof(duk_size_t)) == -1)
        return strdup(pipe_error);

    if(pickle_sz)
    {
        dprintf(4,"in parent_py_call - sending: pickle=%p, pickle_sz=%d\n",pickle, (int)pickle_sz);
        //dprintf(4,"reader=%d, writer=%d\n", finfo->reader, finfo->writer);
        dprintfhex(4,pickle, pickle_sz, "pickle=0x");
        if(forkwrite(pickle, pickle_sz) == -1)
        {
            state = PYLOCK;
            RP_Py_XDECREF(pBytes);
            PYUNLOCK(state);

            return strdup(pipe_error);
        }
        state = PYLOCK;
        RP_Py_XDECREF(pBytes);
        PYUNLOCK(state);
    }

    return parent_read_val(finfo);
}

static PyObject *py_call_in_child(char *fname, PyObject *pModule, PyObject *pArgs, PyObject *kwdict, char **errmsg)
{
    PyObject *pValue=NULL, *pFunc=NULL;
    char *err=NULL;

    if(pModule == NULL)
    {
        err="Error: No Module Found";
        goto end;
    }

    if(!pArgs)
        pArgs=PyTuple_New(0);

    dprintf(4,"in call %p, %s\n", pModule, (fname? fname: "non-method call") );

    if(fname) //method call
        pFunc = PyObject_GetAttrString(pModule, fname);
    else
        pFunc = pModule;

    dprintf_pyvar(4,x,pModule,"got func %s(%p) from %s, function callable = %d\n", fname, pFunc, x, pFunc?(int)PyCallable_Check(pFunc):0);

    if (!pFunc || !PyCallable_Check(pFunc))
    {
        dprintf(4,"error pfunc=%p, callable=%d\n", pFunc, pFunc?(int)PyCallable_Check(pFunc):0);
        err="error calling python function: %s";
        goto end;
    }

    dprintf(4,"CALLING with args=%p, nargs=%d\n", pArgs, (int) (pArgs ? PyTuple_Size(pArgs) : 0) );

    dcheckvar(4,pArgs);
    dprintf_pyvar(4, x, pFunc, "tpcall func=%s\n", x );
    dprintf_pyvar(4,x,pArgs, "tpcall args=%s\n", x );
    dprintf_pyvar(4,x,kwdict,"tpcall kwdict=%s\n", x );

    //pValue = PyObject_CallObject(pFunc, pArgs);
    pValue = Py_TYPE(pFunc)->tp_call(pFunc, pArgs, kwdict);
    dprintf(4,"CALLED\n");

    if(!pValue) {
        err="error calling python function: %s";
        //goto end;
    }

end:
    dprintf(4,"end: pValue=%p\n",pValue);
    RP_Py_XDECREF(pFunc);
    RP_Py_XDECREF(pArgs);
    RP_Py_XDECREF(kwdict);
    if(err)
    {
        char buf[MAX_EXCEPTION_LENGTH];
        asprintf(errmsg, err, get_exception(buf));
    }
    else if (PyErr_Occurred())
    {
        char buf[MAX_EXCEPTION_LENGTH];
        char *exc = get_exception(buf);
        asprintf(errmsg, "Non-fatal error occured while executing '%s': %s", fname, &exc[1]);
    }
    dprintf_pyvar(4, x, pValue, "returning value %p %s\n", pValue, x );
    return pValue;
}

static int child_py_call_write_error(PFI *finfo, char *errmsg)
{
    duk_size_t sz=0;
    int needsfree=0;

    if(!errmsg)
        errmsg="";
    else
        needsfree=1;

    dprintf(4,"writing error msg: '%s'\n", errmsg);
    sz=strlen(errmsg)+1;

    if(forkwrite(&sz, sizeof(duk_size_t)) == -1)
        return 0;

    if(forkwrite(errmsg, sz) == -1)
        return 0;

    if(needsfree)
        free(errmsg);

    return 1;
}

static int child_write_var(PFI *finfo, PyObject *pRes, char *errmsg)
{
    if(!pRes && errmsg)
    {
        if(forkwrite("e", sizeof(char)) == -1)
            return 0;
        if(!child_py_call_write_error(finfo, errmsg))
            return 0;
    }
    else
    {
        int callable=0;
        //for some reason, we need to do this before attempting to pickle
        //after that PyObject_Dir returns NULL with error
        char *funcnames = NULL;
        size_t flen = 0;
        const char *resstr = "";
        size_t str_sz = 1;
        PyObject *pStr=NULL;

        if(pRes)
        {
            funcnames = stringify_funcnames(pRes);
            flen = strlen(funcnames)+1;
            pStr=PyObject_Str(pRes);
            resstr=PyUnicode_AsUTF8(pStr);
            str_sz=strlen(resstr) + 1;
        }

        dprintf(4,"funcs = %s\n", funcnames);

        if(forkwrite("r", sizeof(char)) == -1)
            return 0;

        dprintf(4,"SENDING pRes=%p for storage\n", pRes);
        if(forkwrite(&pRes, sizeof(PyObject *)) == -1)
            return 0;

        if(forkwrite(&str_sz, sizeof(size_t)) == -1)
            return 0;
        
        //dprintf(4,"sending %s, size=%d\n", resstr, (int)str_sz);
        if(forkwrite(resstr, str_sz) == -1)
            return 0;

        RP_Py_XDECREF(pStr);

        if(forkwrite(&flen, sizeof(size_t)) == -1)
            return 0;

        if(flen)
        {
            if(forkwrite(funcnames, flen) == -1)
                return 0;
            free(funcnames);
        }

        if(PyCallable_Check(pRes))
            callable=1;

        if(forkwrite(&callable, sizeof(int)) == -1)
            return 0;

        if(!child_py_call_write_error(finfo, errmsg))
            return 0;
    }
    return 1;
}

static int child_py_call(PFI *finfo)
{
    PyObject *pModule=NULL, *pRes=NULL, *pArgs=NULL, *kwdict=NULL;
    char *fname=NULL;
    char *pickle=NULL;
    Py_ssize_t pickle_sz=0;
    size_t fname_sz = 0;
    char  *errmsg = NULL;
    int ret=0;

    if(forkread(&pModule, sizeof(PyObject *)) == -1)
        return 0;

    dprintf(4,"in child_py_call, pmod=%p\n", pModule);
    if(forkread(&fname_sz, sizeof(size_t)) == -1)
        return 0;

    if(fname_sz) //method call
    {
        REMALLOC(fname, fname_sz);
        if(forkread(fname, fname_sz) == -1)
            return 0;
    }

    if(forkread(&pickle_sz, sizeof(Py_ssize_t)) == -1)
        return 0;

    dprintf(4,"in child_py_call - received, pickle_sz=%d\n", (int)pickle_sz);
    if(pickle_sz > 0)
    {
        REMALLOC(pickle, (size_t) pickle_sz);
        if(forkread(pickle, (size_t)pickle_sz) == -1)
            return 0;

        //dprintf(4,"reader=%d, writer=%d\n", finfo->reader, finfo->writer);
        dprintfhex(4,pickle, pickle_sz, "pickle=0x");
        pArgs=PyPickle_ReadObjectFromString((const char*)pickle, pickle_sz);
        free(pickle);

        if(!pArgs || !PyTuple_Check(pArgs) )
        {
            size_t sz=0;
            const char *errmsg = "failed to decode pickled value";
            dprintf(4,"writing error %s", errmsg);
            //if( pArgs && !PyTuple_Check(pArgs) ) dprintf(4,"its not null, but its not a tuple");
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

        dprintf_pyvar(4,x,pArgs,"pArgs1 = %p, %s\n", pArgs, x );
        // take the dictionary back out
        kwdict = PyTuple_GetItem(pArgs, len-1);
        Py_XINCREF(kwdict);
        len--;
        _PyTuple_Resize(&pArgs, len);

        dprintf_pyvar(4,x,pArgs,"pArgs = %p, %s\n", pArgs, x );

        // fix references in kwdict by searching for a dictionary with prop "___REF_IN_CHILD___"
        while( PyDict_Next(kwdict, &pos, &dkey, &dvalue) )
        {
            dprintf(4,"checking %s->%s\n",PyUnicode_AsUTF8(dkey), PyUnicode_AsUTF8(PyObject_Str(dvalue)) ); 
            if( PyDict_Check(dvalue) && PyDict_Contains(dvalue, key/*___REF_IN_CHILD___*/) == 1 )
            {
                 PyObject *pLong = PyDict_GetItem(dvalue, key);        //get the pointer as long
                 PyObject *pRef = (PyObject *)PyLong_AsVoidPtr(pLong); //convert back to pointer
                 dprintf(4,"Got a ref in kwdict, pRef=%p\n", pRef);
                 dprintf(4,"Setting %s to %s\n", PyUnicode_AsUTF8(dkey), PyUnicode_AsUTF8(PyObject_Str(pRef)) );
                 if(PyDict_SetItem(kwdict, dkey, pRef))                //put it as a PyObject in kwdict
                     dprintf(4,"Error HERE\n");
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
                 dprintf_pyvar(4,x,pRef,"Got a ref in args, pRef=%p %s\n", pRef, x);
                 dcheckvar(4,pRef);
                 PyTuple_SetItem(pArgs, i, pRef);
                 Py_XINCREF(pRef);
                 //RP_Py_XDECREF(pLong); -- this is bad.
            }
            i++;
        }
        RP_Py_XDECREF(key);
    }

    pRes=py_call_in_child(fname, pModule, pArgs, kwdict, &errmsg);
    free(fname);

    ret = child_write_var(finfo, pRes, errmsg);

    return ret;
}

static char *parent_get(PyObject *parentVal, const char *key)
{
    PFI *finfo = check_fork();
    char *pipe_error="pipe error";
    size_t sz = strlen(key)+1;

    if(forkwrite("g", sizeof(char)) == -1)
        return strdup(pipe_error);

    if(forkwrite(&parentVal, sizeof(PyObject*)) == -1)
        return strdup(pipe_error);

    if(forkwrite(&sz, sizeof(size_t)) == -1)
        return strdup(pipe_error);

    if(forkwrite(key, sz) == -1)
        return strdup(pipe_error);

    return parent_read_val(finfo);

}

static int child_get(PFI *finfo)
{
    PyObject *parentValue=NULL, *pValue=NULL;
    int ret=0;
    char *key=NULL;
    size_t sz=0;

    if(forkread(&parentValue, sizeof(PyObject *)) == -1)
        return 0;


    if(forkread(&sz, sizeof(size_t)) == -1)
        return 0;

    REMALLOC(key, sz);

    if(forkread(key, sz) == -1)
        return 0;

    dprintf(4,"looking for %s in child\n", key);
    pValue = PyObject_GetAttrString(parentValue, key);
    if(!pValue)
        PyErr_Clear();

    if(!pValue && PyDict_Check(parentValue))
    {
        PyErr_Clear();
        pValue = PyDict_GetItemString(parentValue, key);
        if(pValue)
        {
            Py_INCREF(pValue); // most confusingly, does not provide new reference like PyObject_GetAttrString
            dprintf(4,"NEW ref for %p\n", pValue);
        }
        else
            PyErr_Clear();
    }

    ret = child_write_var(finfo, pValue, NULL);
    return ret;
}


static int parent_pid=0;

/* in child process, loop and await commands */
static void do_fork_loop(PFI *finfo)
{
    while(1)
    {
        char command='\0';
        int ret = 0;

        if( kill(parent_pid,0) )
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
        //   py_call   p
        //   importString     s
        //   import           i
        //   finalizer        f
        //   get              g
        //   toValue          v
        dprintf(4,"in fork_loop -- COMMAND='%c'\n", command);

        switch(command)
        {
            case 'p':
                ret = child_py_call(finfo);
                break;
            case 's':
                ret = child_import(finfo, 1);
                break;
            case 'i':
                ret = child_import(finfo, 0);
                break;
            case 'f':
                ret = child_finalizer(finfo);
                break;
            case 'g':
                ret = child_get(finfo);
                break;
            case 'v':
                ret = child_get_val(finfo);
                break;
        }

        if(!ret)
        {
            fprintf(stderr,"error in python fork\n");
            exit(1);
        }
#if rpydebug > 0
        if(PyErr_Occurred())
        {
            char buf[MAX_EXCEPTION_LENGTH];
            char *exc = get_exception(buf);
            dprintf(1, "EXCEPTION: '%c' %s\n", command, &exc[1]);
            exit(1);
        }
#endif
        RP_EMPTY_STACK(finfo->ctx);
    }
}


//static char *scr_txt = "rampart.utils.fprintf(rampart.utils.stderr,'YA, were in\\n');var p=require('rampart-python');p.__helper();\n";
static char *scr_txt = "var p=require('rampart-python');p.__helper(%d,%d,%d);\n";

static PFI *check_fork()
{
    int pidstatus;
    int threadno = get_thread_num();
    PFI *finfo = NULL;
    dprintf(5,"check_fork(), threadno=%d\n", threadno);

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
            dprintf(4,"ERROR:  we should have child process %d, but appears to be dead\n", finfo->childpid);
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
            sprintf(script, scr_txt, par2child[0], child2par[1], get_thread_num());
            dprintf(4,"executing %s -c %s\n", rampart_exec, script);

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
            dprintf(4,"reader=%d, writer=%d\n", finfo->reader, finfo->writer);
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

    start_pytojs(ctx);

    if(pValue)
    {
        start_pytojs(ctx);
        state = PYLOCK;
        dprintf_pyvar(4,x,pValue,"p_to_val for %s\n", x);
        push_ptype(ctx, pValue);
        PYUNLOCK(state);
    }
    else
        duk_push_null(ctx);

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
        dprintf_pyvar(4,x,pValue,"decref for %p (%s) with refcnt = %d\n", pValue, x, (int)Py_REFCNT((pValue)));
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

/*  object must be at idx == -1  */
static void put_attributes(duk_context *ctx, PyObject *pValue)
{
    PyObject *pobj = PyObject_Dir(pValue);
    /* we cannot put a proxy object on a function */
    int parent_is_callable = PyCallable_Check(pValue);

    dprintf(4,"%schecking for functions in object. type: %s\n", pobj?"":"FAIL ", Py_TYPE(pValue)->tp_name);

    if(!pobj)
        return;

    Py_ssize_t len=PyList_Size(pobj), i=0;
    PyObject *value=NULL, *pProp;

    while( i<len )
    {
        const char *fname;
        size_t l;

        value=PyList_GetItem(pobj, i);
        fname = PyUnicode_AsUTF8(value);
        l=strlen(fname);
        if(l>3 && *fname=='_' && fname[1]=='_' && fname[l-2]=='_' && fname[l-1]=='_')
        {
            i++;
            continue;        
        }

        pProp = PyObject_GetAttr(pValue, value);
        dprintf(4,"checking %s - is %s - iscallable:%d\n", PyUnicode_AsUTF8(value), (pProp? Py_TYPE(pProp)->tp_name:"pProp=NULL"), pProp ? PyCallable_Check(pProp):0);

        /* inherited funcs in a class come up as null (at least the first time) */
        if(!pProp)
        {
            PyObject* pBase = (PyObject*) pValue->ob_type->tp_base;
            if(pBase)
            {
                pProp = PyObject_GetAttr(pBase, value);
                dprintf(4,"Got from base, pProp=%p\n", pProp);
            }
        }

        if(pProp)
        {
            if(PyCallable_Check(pProp))
            {
                PyObject *Str = PyObject_Str(pProp);
                push_python_function_as_method(ctx, fname, pValue, (char*)PyUnicode_AsUTF8(Str));
                duk_put_prop_string(ctx, -2, fname);
                RP_Py_XDECREF(Str);
            }
            else if(parent_is_callable)
            {
                make_pyval(pProp, _p_to_string, _p_to_value, NULL, NULL, NULL);
                duk_put_prop_string(ctx, -2, fname);
            }
        }

        i++;
        RP_Py_XDECREF(pProp);
    }
}

#define py_throw_fmt(fmtstr) do{\
    char buf[MAX_EXCEPTION_LENGTH];\
    char *exc = get_exception(buf);\
    PYUNLOCK(state);\
    RP_THROW(ctx, (fmtstr), exc);\
}while(0)



static void get_pyval_and_push(duk_context *ctx, duk_idx_t idx, const char *key)
{
    PyObject *pValue=NULL, *parentValue=NULL;
    PyGILState_STATE state;

    if(!duk_get_prop_string(ctx, idx, DUK_HIDDEN_SYMBOL("pvalue")))
    {
        char *err=NULL;
        duk_pop(ctx);
        if(!duk_get_prop_string(ctx, idx, DUK_HIDDEN_SYMBOL("pref")))
            RP_THROW(ctx, "failed to retrieve python pointer in proxy.get"); //never will happen

        parentValue = duk_get_pointer(ctx, -1);

        if(!parentValue)
        {
            dprintf(4,"looking for %s in NULL, returning undefined\n", key);
            duk_push_undefined(ctx);
            return;
        }
        dprintf(4,"looking for %s in child\n", key);
        err=parent_get(parentValue, key);

        if(err)
        {
            // RP_THROW with a free in the middle
            duk_push_error_object(ctx, DUK_ERR_SYNTAX_ERROR, "%s", err);
            free(err);
            (void) duk_throw(ctx);\
        }
        return;
    }

    parentValue = duk_get_pointer(ctx, -1);        
    duk_pop(ctx);

    state=PYLOCK;
    dprintf_pyvar(4,x,parentValue,"looking in %s\n", x);

    // try to get properties first.
    pValue = PyObject_GetAttrString(parentValue, key);
    if(!pValue)
        PyErr_Clear();

    // if it is a dictionary, try for item second.
    // if item has same name as a property, you can always retrieve with
    // mydict.get('item_name')
    if(!pValue && PyDict_Check(parentValue))
    {
        pValue = PyDict_GetItemString(parentValue, key);
        if(pValue)
        {
            Py_INCREF(pValue); // most confusingly, does not provide new reference like PyObject_GetAttrString
            dprintf(4,"NEW ref for %p\n", pValue);
        }
    }

    if(!pValue)
    {
        dprintf(4,"not found\n");
        PYUNLOCK(state);
        duk_push_undefined(ctx);
        return;
    }
    dprintf_pyvar(4,x,pValue,"found %s\n", x);

    make_pyval(pValue, _p_to_string, _p_to_value, NULL, NULL, NULL);

    // in order for finalizer to be run on this, it needs to be properly in the object
    duk_push_sprintf(ctx, "\xff%p", pValue);
    duk_dup(ctx, -2);
    duk_put_prop(ctx, 0);

    PYUNLOCK(state);
}


static duk_ret_t _proxyget(duk_context *ctx)
{
    const char *key = duk_get_string(ctx,1);  //the property we are trying to retrieve

    dprintf(4,"looking for %s in proxy get\n", key);
    if( duk_get_prop_string(ctx, 0, key) ) //see if it already exists
    {
        dprintf(4,"found %s in backing object\n", key);
        return 1;
    }
    duk_pop(ctx);

    get_pyval_and_push(ctx, 0, key);    

    return 1;
}

static duk_ret_t duk_make_proxy(duk_context *ctx)
{
    duk_push_object(ctx);

    //duk_push_c_function(ctx, ownkeys, 1);
    //duk_put_prop_string(ctx, -2, "ownKeys");
 

    duk_push_c_function(ctx, _proxyget, 2);
    duk_put_prop_string(ctx, -2, "get");
    //duk_push_c_function(ctx, del, 2);
    //duk_put_prop_string(ctx, -2, "deleteProperty");
    //duk_push_c_function(ctx, put, 4);
    //duk_put_prop_string(ctx, -2, "set");
    duk_push_proxy(ctx, 0);
    return 1;
}

static void make_proxy(duk_context *ctx)
{
    duk_push_c_function(ctx, duk_make_proxy, 1);
    duk_pull(ctx, -2);    
    duk_new(ctx, 1);
}

static void make_pyfunc(duk_context *ctx, PyObject *pFunc)
{
    PyGILState_STATE state;
    duk_push_c_function(ctx, py_call, DUK_VARARGS);

    put_func_attributes(ctx, pFunc, NULL, NULL);

    state=PYLOCK;
    put_attributes(ctx, pFunc);
    PYUNLOCK(state);
}

static duk_ret_t _py_call(duk_context *ctx, int is_method)
{
    PyObject *pModule=NULL, *pValue=NULL, *pArgs=NULL, *pFunc=NULL, *kwdict=NULL;
    duk_idx_t i=1, top=duk_get_top(ctx);
    const char *err=NULL, *fname = NULL;
    PyGILState_STATE state;
    int thrno, haskw=0;
    if(is_method)
        fname=REQUIRE_STRING(ctx, 0, "module.call: first argument must be a String");
    else
    {
        // fill the spot so indexing is correct below
        duk_push_undefined(ctx);
        duk_insert(ctx, 0);
    }

    duk_push_current_function(ctx);

    // get our calling thread, make sure it matches importing thread
    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("thrno")))
        RP_THROW(ctx, "No Module Found");
    thrno=duk_get_int(ctx, -1);
    duk_pop(ctx);

    if(thrno != get_thread_num() )
        RP_THROW(ctx, "Cannot execute python function from a module imported in a different thread");

    if(!is_child && must_fork)
    {
        if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pref")))
            RP_THROW(ctx, "No Module Found");;
        pModule = (PyObject *) duk_get_pointer(ctx, -1);
        duk_pop_2(ctx);
        dprintf(4,"in py_call, pmod=%p, ctx=%p\n", pModule, ctx);

        char *err = parent_py_call(pModule, fname);

        if(err)
        {
            // RP_THROW with a free in the middle
            duk_push_error_object(ctx, DUK_ERR_SYNTAX_ERROR, "%s", err);
            free(err);
            (void) duk_throw(ctx);\
        }
        return 1;
    }

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pvalue"));
    pModule = (PyObject *) duk_get_pointer(ctx, -1);
    if(pModule == NULL)
        RP_THROW(ctx, "No Module Found");
    duk_pop_2(ctx);

    state = PYLOCK;

    if(is_method)
        pFunc = PyObject_GetAttrString(pModule, fname);
    else
        pFunc = pModule;

    if (!pFunc || !PyCallable_Check(pFunc))
    {
        err="error calling python function: %s";
        goto end;
    }
        
    // create the jsobj->pyobj map.  Used to detect and fix cyclical references in type_to_pytype
    start_jstopy(ctx);

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

    start_jstopy(ctx);

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

    if(!pValue) {
        err="error calling python function: %s";
        goto end;
    }

    PYUNLOCK(state);

    if(PyCallable_Check(pValue))
        make_pyfunc(ctx, pValue);
    else
        make_pyval(pValue, _p_to_string, _p_to_value, NULL, NULL, NULL);

    state=PYLOCK;    

    end:  //if goto, we never unlocked

    RP_Py_XDECREF(pFunc);
    RP_Py_XDECREF(pArgs);
    if(err)
        py_throw_fmt(err); //includes PYUNLOCK(state);
    else if (PyErr_Occurred())
    {
        // well, we have something valid since pValue is not NULL.  And it might work.
        // example:
        //	   var pathlib = python.import('pathlib');
        //     var p = pathlib.Path("./");
        //   produces an error "'PosixPath' object has no attribute 'write_text'"
        //   but otherwise functions correctly.  So don't throw, but do log the error.
        // BTW: that example above no longer happens.  Seems that accessing any inherited members of a
        // class just once (as we do in put_attributes() above) lets python access all its inherited members.
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

static duk_ret_t py_call_method(duk_context *ctx)
{
    return _py_call(ctx, 1);
}

static duk_ret_t py_call(duk_context *ctx)
{
    return _py_call(ctx, 0);
}

static duk_ret_t named_call(duk_context *ctx)
{
    duk_push_current_function(ctx);
    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pyfunc_name")))
        RP_THROW(ctx, "Internal error getting python function name");
    dprintf(4, "in named call, doing %s\n", duk_get_string(ctx, -1));
    duk_insert(ctx, 0);
    duk_pop(ctx); //current_func
    return py_call_method(ctx);
}


/* object must be at idx == -1 */
static void put_callPyFunc(duk_context *ctx, PyObject *pModule)
{
    duk_push_c_function(ctx, py_call_method, DUK_VARARGS);

    if(!is_child && must_fork)
        put_func_attributes(ctx, NULL, pModule, NULL);
    else
        put_func_attributes(ctx, pModule, NULL, NULL);

    // call is property of return object
    duk_put_prop_string(ctx, -2, "callPyFunc");
}


static duk_ret_t _import (duk_context * ctx, int type)
{
    // 0=import, 1=importString, 2=importFile
    const char *fnamesuff = (!type ? "" : (type==1?"String":"File"));
    const char *str = REQUIRE_STRING(ctx, 0, "python.import%s: First argument must be a string", fnamesuff);
    PyObject *pModule;
    PyGILState_STATE state;
    const char *script=str, *modname=str;
    char *scriptname=RP_script;

    if(!python_is_init)
        rp_duk_python_init(ctx);

    // type 0 - modname and script(name) are the same
    // type 1 modname is optionally given as arg1, script is script
    if(type==1)
    {
        char *s;
        if(duk_is_string(ctx,1))
            modname=duk_get_string(ctx,1);
        else
            modname="module_from_string";

        s=strrchr(RP_script, '/');
        if(!s)
            s=RP_script;

        scriptname = strcatdup(strdup(RP_script)," (imported from JavaScript String)");
    }
    else if(type==2)// script is file at arg0, modname is arg0
    {
        //read the file.  only filename is on the stack.
        scriptname=(char*)duk_get_string(ctx, 0);
        duk_push_true(ctx);
        duk_rp_read_file(ctx);
        script=duk_get_string(ctx, -1);
    }

    if(!is_child && must_fork)
    {
        char *obj_fnames=NULL, *funcstring=NULL;
        char *pscript = (char*)script;

        if(type)
        {
            // if filestring or string, prepend modname to string.
            pscript = strcatdup( strdup(modname), "\xff");
            pscript = strcatdup( pscript, (char*)scriptname);
            pscript = strcatdup( pscript, "\xff");
            pscript = strcatdup( pscript, (char*)script);
        }

        pModule = parent_import(ctx, pscript, type, &obj_fnames, &funcstring);

        if(type)
            free(pscript);

        if(!pModule)
        {
            RP_THROW(ctx,"error loading python module: %s", duk_get_string(ctx, -1) );
        }

        duk_push_object(ctx);

        // Put items if dictionary. Put attributes. Put functions.
        put_attributes_from_string(ctx, pModule, obj_fnames);
        free(obj_fnames);
        // put toString, valueOf, etc
        put_func_attributes(ctx, NULL, pModule, funcstring);
        free(funcstring);

        // hidden pmod as property of object
        duk_push_pointer(ctx, (void*)pModule);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pref"));
    }
    else
    {
        state=PYLOCK;
        if(type)
        {
            // compile and execute string.
            PyObject *pCode = Py_CompileString(script, scriptname, Py_file_input);

            if(!pCode)
            {
                if(type==1)
                    py_throw_fmt("error compiling python string: %s");
                else
                    py_throw_fmt("error compiling python file: %s");
            }

            pModule = PyImport_ExecCodeModule(modname, pCode );
        }
        else
            // import/execute module
            pModule = PyImport_ImportModule(script);

        if(!pModule)
                py_throw_fmt("error loading python module: %s");

        duk_push_object(ctx);

        // Put items if dictionary. Put attributes. Put functions.
        put_attributes(ctx, pModule);
        // put toString, valueOf, etc
        put_func_attributes(ctx, pModule, NULL , NULL);
        PYUNLOCK(state);

        // hidden pmod as property of object
//        duk_push_pointer(ctx, (void*)pModule);
//        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pvalue"));

    }

    put_callPyFunc(ctx, pModule);

    duk_push_int(ctx, get_thread_num() );
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("thrno"));

    // finalizer for object
    duk_push_c_function(ctx, pvalue_finalizer, 1);
    duk_set_finalizer(ctx, -2);
    make_proxy(ctx);

    if(type==1)
        free(scriptname);

    return 1;
}


static duk_ret_t RP_Pimport (duk_context * ctx)
{
    return _import(ctx, 0);
}

static duk_ret_t RP_PimportString (duk_context * ctx)
{
    return _import(ctx, 1);
}

static duk_ret_t RP_PimportFile (duk_context * ctx)
{
    return _import(ctx, 2);
}


static duk_ret_t helper(duk_context *ctx)
{
    PFI *finfo=&finfo_d;


    setproctitle("rampart py_helper");

    finfo->reader = REQUIRE_UINT(ctx,0, "Error, this function is meant to be run upon forking only");
    finfo->writer = REQUIRE_UINT(ctx,1, "Error, this function is meant to be run upon forking only");
    /* to help with debugging, get parent's thread num */
    is_child = REQUIRE_UINT(ctx,2, "Error, this function is meant to be run upon forking only");

    // it _should_ always be > 0
    if(!is_child)
        is_child=4097;

    dprintf(4,"reader=%d, writer=%d\n", finfo->reader, finfo->writer);

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
    duk_push_c_function(ctx, helper, 3);
    duk_put_prop_string(ctx, -2, "__helper");

  return 1;
}
