/*-
 * Copyright (c) 2006 Allan Saddi <allan@saddi.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 */

#include <Python.h>
#include <node.h>
#include <locale.h>
#include <wchar.h>

#include <assert.h>
#include <ctype.h>

#include "wsgi-int.h"
#include "lsapilib.h"

#include "interp.h"

const char *wsgiPyVersion = PY_VERSION;
PyObject *wsgiStderr;
const char *wsgiScriptName = "";
int wsgiScriptNameLen = 0;

static PyThreadState *_main;

#define LOG_FLAG   (LSAPI_LOG_TIMESTAMP_FULL|LSAPI_LOG_FLAG_ERROR|LSAPI_LOG_PID)
#define LOG_ERR(...)   LSAPI_Log(LOG_FLAG, __VA_ARGS__)


/* Assumes c is a valid hex digit */
static inline int toxdigit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}


/* Unquote an escaped path */
const char *wsgiUnquote(const char *s)
{
    int len = strlen(s);
    char *result, *t;

    if ((result = PyMem_Malloc(len + 1)) == NULL)
        return NULL;

    t = result;
    while (*s)
    {
        if (*s == '%')
        {
            if (s[1] && s[2] && isxdigit(s[1]) && isxdigit(s[2]))
            {
                * (t++) = (toxdigit(s[1]) << 4) | toxdigit(s[2]);
                s += 3;
            }
            else
                * (t++) = * (s++);
        }
        else
            * (t++) = * (s++);
    }
    *t = '\0';

    return result;
}


int wsgiPutEnv(Request *self, const char *key, const char *value)
{
    PyObject *val;
    int ret;

#if PY_MAJOR_VERSION < 3
    if ( ( val = PyBytes_FromString ( value ) ) == NULL )
#else
    if ( ( val = PyUnicode_FromString ( value ) ) == NULL )
#endif        
        return -1;
    ret = PyDict_SetItemString(self->environ, key, val);
    Py_DECREF(val);
    if (ret)
        return -1;
    return 0;
}


static void sendResponse500(void *ctxt)
{
    static const char *headers[] =
    {
        "Content-Type", "text/html; charset=iso-8859-1",
    };
    static const char *body =
        "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n"
        "<html><head>\n"
        "<title>500 Internal Error</title>\n"
        "</head><body>\n"
        "<h1>Internal Error</h1>\n"
        "<p>The server encountered an unexpected condition which\n"
        "prevented it from fulfilling the request.</p>\n"
        "</body></html>\n";

    if (!wsgiSendHeaders(ctxt, 500, 1, headers))
        wsgiSendBody(ctxt, (uint8_t *) body, strlen(body));
}


static void Request_clear(Request *self)
{
    PyObject *tmp;

    tmp = self->result;
    self->result = NULL;
    Py_XDECREF(tmp);

    tmp = self->headers;
    self->headers = NULL;
    Py_XDECREF(tmp);

    tmp = self->input;
    self->input = NULL;
    Py_XDECREF(tmp);

    tmp = self->environ;
    self->environ = NULL;
    Py_XDECREF(tmp);
}


static void Request_dealloc(Request *self)
{
    Request_clear(self);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


static PyObject *Request_new(PyTypeObject *type, PyObject *args,
                             PyObject *kwds)
{
    Request *self;

    self = (Request *) type->tp_alloc(type, 0);
    if (self != NULL)
    {
        self->environ = PyDict_New();
        if (self->environ == NULL)
        {
            Py_DECREF(self);
            return NULL;
        }

        self->input = NULL;
        self->statusCode = 200;
        self->headers = NULL;
        self->result = NULL;
        self->headers_sent = 0;
    }

    return (PyObject *) self;
}


/* Constructor. Accepts the context CObject as its sole argument. */
static int Request_init(Request *self, PyObject *args, PyObject *kwds)
{
    PyObject *context_obj, *args2;

    if (!PyArg_ParseTuple(args, "O!", &PyCapsule_Type, &context_obj))
        return -1;

    self->context = PyCapsule_GetPointer(context_obj, "Request");

    if ((args2 = Py_BuildValue("(Oi)", self,
                               wsgiGetContentLength(self->context))) == NULL)
        return -1;

    self->input = PyObject_CallObject((PyObject *) &InputStream_Type, args2);
    Py_DECREF(args2);
    if (self->input == NULL)
        return -1;

    return wsgiPopulateEnviron(self);
}


/* start_response() callable implementation */
static PyObject *Request_start_response(Request *self, PyObject *args)
{
    char *psStatus;
    PyObject *headers, *exc_info = NULL;
    PyObject *tmp;

    if (!PyArg_ParseTuple(args, "sO!|O:start_response", &psStatus,
                          &PyList_Type, &headers, &exc_info))
    {
        LOG_ERR(
                "Request_start_response() ParseTuple 'sO!|O:start_response' failed.\n");
        return NULL;
    }

    if (exc_info != NULL && exc_info != Py_None)
    {
        /* If the headers have already been sent, just propagate the
           exception. */
        if (self->headers_sent)
        {
            PyObject *type, *value, *tb;
            if (!PyArg_ParseTuple(exc_info, "OOO", &type, &value, &tb))
            {
                LOG_ERR("Request_start_response() ParseTuple 'OOO' failed.\n");
                return NULL;
            }
            Py_INCREF(type);
            Py_INCREF(value);
            Py_INCREF(tb);
            PyErr_Restore(type, value, tb);
            return NULL;
        }
    }
    else if (self->headers != NULL)
    {
        LOG_ERR("Request_start_response headers already set.\n");
        PyErr_SetString(PyExc_AssertionError, "headers already set");
        return NULL;
    }

    /* TODO validation of status and headers */

    self->statusCode = strtol(psStatus, NULL, 10);

    tmp = self->headers;
    Py_INCREF(headers);
    self->headers = headers;
    Py_XDECREF(tmp);

    return PyObject_GetAttrString((PyObject *) self, "write");
}


/* Sends headers. Assumes self->status and self->headers are valid. */
static int _wsgiSendHeaders(Request *self)
{
    int headerCount;
    const char **headers;
    int i, j;

    headerCount = PyList_Size(self->headers);
    if (PyErr_Occurred())
        return -1;

    /* NB: 1 extra header for Content-Length */
    if ((headers = PyMem_Malloc(sizeof(*headers) *
                                (headerCount * 2 + 2))) == NULL)
        return -1;

    for (i = 0, j = 0; i < headerCount; i++)
    {
        PyObject *item;
        const char *header, *value;

        if ((item = PyList_GetItem(self->headers, i)) == NULL)
            goto bad;

        if (!PyArg_ParseTuple(item, "ss", &header, &value))
            goto bad;

        headers[j++] = header;
        headers[j++] = value;
    }

    if (wsgiSendHeaders(self->context, self->statusCode,
                        headerCount, headers))
        goto bad;

    PyMem_Free(headers);
    return 0;

bad:
    PyMem_Free(headers);
    return -1;
}


/* Send a chunk of data */
static inline int wsgiWrite(Request *self, const char *data, int len)
{
    if (len)
    {
        if (wsgiSendBody(self->context, (uint8_t *) data, len))
            return -1;
    }
    return 0;
}


/* write() callable implementation */
static PyObject *Request_write(Request *self, PyObject *args)
{
    const char *data;
    int dataLen;

    if (self->headers == NULL)
    {
        PyErr_SetString(PyExc_AssertionError, "write() before start_response()");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "s#:write", &data, &dataLen))
        return NULL;

    /* Send headers if necessary */
    if (!self->headers_sent)
    {
        if (_wsgiSendHeaders(self))
            return NULL;
        self->headers_sent = 1;
    }

    if (wsgiWrite(self, data, dataLen))
        return NULL;

    Py_INCREF(Py_None);
    return Py_None;
}

/* Send a wrapped file using wsgiSendFile */
static int
wsgiSendFileWrapper(Request *self, FileWrapper *wrapper)
{
    PyObject *pFileno, *args, *pFD;
    int fd;

    /* file-like must have fileno */
    if (!PyObject_HasAttrString((PyObject *) wrapper->filelike, "fileno"))
        return 1;

    if ((pFileno = PyObject_GetAttrString((PyObject *) wrapper->filelike,
                                          "fileno")) == NULL)
        return -1;

    if ((args = PyTuple_New(0)) == NULL)
    {
        Py_DECREF(pFileno);
        return -1;
    }

    pFD = PyObject_CallObject(pFileno, args);
    Py_DECREF(args);
    Py_DECREF(pFileno);
    if (pFD == NULL)
        return -1;

#if PY_MAJOR_VERSION >= 3
    fd = PyLong_AsLong(pFD);
#else
    fd = PyInt_AsLong(pFD);
#endif
    Py_DECREF(pFD);
    if (PyErr_Occurred())
        return -1;

    /* Send headers if necessary */
    if (!self->headers_sent)
    {
        if (_wsgiSendHeaders(self))
            return -1;
        self->headers_sent = 1;
    }

    if (wsgiSendFile(self->context, fd))
        return -1;

    return 0;
}


/* Send the application's response */
static int wsgiSendResponse(Request *self, PyObject *result)
{
    PyObject *iter;
    PyObject *item;
    int ret;

    /* Check if it's a FileWrapper */
    if (result->ob_type == &FileWrapper_Type)
    {
        ret = wsgiSendFileWrapper(self, (FileWrapper *) result);
        if (ret < 0)
            return -1;
        if (!ret)
            return 0;
        /* Fallthrough */
    }

    iter = PyObject_GetIter(result);
    if (iter == NULL)
        return -1;

    while ((item = PyIter_Next(iter)))
    {
        int dataLen;
        const char *data;
        if (PyUnicode_Check(item))
        {
            PyObject *pBytes = PyUnicode_AsASCIIString(item);
            Py_DECREF(item);
            item = pBytes;
        }


        dataLen = PyBytes_Size(item);
        if (PyErr_Occurred())
        {
            Py_DECREF(item);
            break;
        }

        if (dataLen)
        {
            if ((data = PyBytes_AsString(item)) == NULL)
            {
                Py_DECREF(item);
                break;
            }

            /* Send headers if necessary */
            if (!self->headers_sent)
            {
                if (_wsgiSendHeaders(self))
                {
                    Py_DECREF(item);
                    break;
                }
                self->headers_sent = 1;
            }

            if (wsgiWrite(self, data, dataLen))
            {
                Py_DECREF(item);
                break;
            }
        }

        Py_DECREF(item);
    }

    Py_DECREF(iter);

    if (PyErr_Occurred())
        return -1;

    /* Send headers if they haven't been sent at this point */
    if (!self->headers_sent)
    {
        if (_wsgiSendHeaders(self))
            return -1;
        self->headers_sent = 1;
    }

    return 0;
}


/* Ensure application's iterator's close() method is called */
static void wsgiCallClose(PyObject *result)
{
    PyObject *type, *value, *traceback;
    PyObject *pClose, *args, *ret;

    /* Save exception state */
    PyErr_Fetch(&type, &value, &traceback);

    if (PyObject_HasAttrString(result, "close"))
    {
        pClose = PyObject_GetAttrString(result, "close");
        if (pClose != NULL)
        {
            args = PyTuple_New(0);
            if (args != NULL)
            {
                ret = PyObject_CallObject(pClose, args);
                Py_DECREF(args);
                Py_XDECREF(ret);
            }
            Py_DECREF(pClose);
        }
    }

    /* Restore exception state */
    PyErr_Restore(type, value, traceback);
}


static PyMethodDef Request_methods[] =
{
    {
        "start_response", (PyCFunction) Request_start_response, METH_VARARGS,
        "WSGI start_response callable"
    },
    {
        "write", (PyCFunction) Request_write, METH_VARARGS,
        "WSGI write callable"
    },
    { NULL }
};


PyTypeObject Request_Type =
{
    PyVarObject_HEAD_INIT(NULL, 0)
    "lsapi_wsgi.Request",        /*tp_name*/
    sizeof(Request),           /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor) Request_dealloc,   /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "WSGI Request class",      /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    Request_methods,           /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc) Request_init,   /* tp_init */
    0,                         /* tp_alloc */
    Request_new,               /* tp_new */
};


static PyMethodDef wsgisup_methods[] =
{
    { NULL }
};


#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef wsgisup_moduledef =
{
    PyModuleDef_HEAD_INIT,
    "lsapi_wsgi",
    NULL,
    0,
    wsgisup_methods,
    NULL,
    NULL,
    NULL,
    NULL
};


#define INITERROR return NULL
#else

#define INITERROR return
#ifndef PyMODINIT_FUNC
#define PyMODINIT_FUNC void
#endif

#endif


PyMODINIT_FUNC init_wsgisup(void)
{
    PyObject *m = NULL;

    if (PyType_Ready(&Request_Type) < 0)
        INITERROR;
    if (PyType_Ready(&InputStream_Type) < 0)
        INITERROR;
    if (PyType_Ready(&FileWrapper_Type) < 0)
        INITERROR;

#if PY_MAJOR_VERSION >= 3
    m = PyModule_Create(&wsgisup_moduledef);
#else
    m = Py_InitModule3("lsapi_wsgi", wsgisup_methods,
                       "WSGI C support module");
#endif

    if (m == NULL)
        INITERROR;

    Py_INCREF(&Request_Type);
    PyModule_AddObject(m, "Request", (PyObject *) &Request_Type);

    Py_INCREF(&InputStream_Type);
    PyModule_AddObject(m, "InputStream", (PyObject *) &InputStream_Type);

    Py_INCREF(&FileWrapper_Type);
    PyModule_AddObject(m, "FileWrapper", (PyObject *) &FileWrapper_Type);

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}


void wsgiCleanup(void)
{
    PyEval_RestoreThread(_main);
    Py_XDECREF(wsgiStderr);
    Py_Finalize();
}


static PyObject *importModule(const char *name, const char *filename)
{
    FILE *fp = NULL;
    PyObject *m = NULL;
    PyObject *co = NULL;
    struct _node *n = NULL;

    fp = fopen(filename, "r");
    if (!fp)
    {
        LOG_ERR("Fail to open file %s. \n", filename);
        return NULL;
    }

    n = PyParser_SimpleParseFile(fp, filename, Py_file_input);
    fclose(fp);
    if (PyErr_Occurred())
        PyErr_Print();


    co = (PyObject *)PyNode_Compile(n, filename);
    if (PyErr_Occurred())
        PyErr_Print();
    PyNode_Free(n);

    if (co)
    {
        m = PyImport_ExecCodeModuleEx((char *)name, co, (char *)filename);
        if (PyErr_Occurred())
            PyErr_Print();
    }
    else
        LOG_ERR("Fail to compile file %s. \n", filename);

    Py_XDECREF(co);

    Py_XINCREF(m);
    return m;
}


#if PY_MAJOR_VERSION >= 3
wchar_t *to_wchar(const char *str)
{
    int len = strlen(str) + 1;
    wchar_t *ret = malloc(sizeof(wchar_t) * len);
    if (ret)
        mbstowcs(ret, str, len);
    return ret;
}
#else
#define to_wchar(str)  ((char *)str)
#endif


void python_init()
{
#if PY_MAJOR_VERSION >= 3
    setlocale(LC_CTYPE, "");
#endif
    const char *python_home = getenv("PYTHONHOME");
    if (python_home)
        Py_SetPythonHome(to_wchar(python_home));
    const char *python_bin = getenv("LS_PYTHONBIN");
    if (python_bin)
        Py_SetProgramName(to_wchar(python_bin));
    Py_Initialize();
    PyEval_InitThreads();
    const char *wsgi_root = getenv("WSGI_ROOT");
    if (wsgi_root)
        chdir(wsgi_root);
}


void wsgiInit()
{
    python_init();

    //init the Types, do it here???
    init_wsgisup();

    _main = PyEval_SaveThread();
    wsgi_interpreters = PyDict_New();

}


PyObject *wsgiImport(const char *module_name, int deref)
{
    PyObject *name, *module;
    name = PyUnicode_FromString(module_name);
    if (name == NULL)
        return NULL;

    module = PyImport_Import(name);
    Py_XDECREF(name);
    if (deref && module)
        Py_XDECREF(module);
    return module;
}


//Just for sub interpreter init
int wsgiSubInit (const char *progName )
{
    PyObject *sys_module;

#if PY_MAJOR_VERSION >= 3
    wchar_t *argv[1];
    
    //setlocale(LC_CTYPE,"");
    argv[0] = to_wchar(progName);
    if (argv[0])
    {
        PySys_SetArgv( 1, argv ); /* make sure sys.argv gets set */
        free(argv[0]);
    }
#else
    char *argv[1];
    argv[0] = (char *)progName;
    PySys_SetArgv(1, argv); /* make sure sys.argv gets set */
#endif
    if (wsgiImport("threading", 1) != NULL 
        && (sys_module = wsgiImport("sys", 0)) != NULL)
    {
        wsgiStderr = PyObject_GetAttrString(sys_module, "stderr");
        Py_DECREF(sys_module);
        if (wsgiStderr != NULL)
            return 0;
    }

    PyErr_Print();
    return -1;
}


PyObject *wsgiLoadModule(const char *moduleName)
{
    PyObject *pModule;
    char baseName[PATH_MAX] = {0};
    int pos = -1;
    const char *p = strrchr(moduleName, '/');
    if (p)
        strncpy(baseName, p + 1, sizeof(baseName));
    else
        strncpy(baseName, moduleName, sizeof(baseName));

    p = strchr(baseName, '.');
    if (p)
    {
        pos = p - baseName;
        baseName[pos] = 0x00;
    }

    pModule = importModule(baseName, moduleName);
//  fprintf(stdout, "wsgiLoadModule %s %s\n", baseName, moduleName);
    return pModule;
}


PyObject *wsgiInitModule(PyObject *modict, const char *moduleName, 
                         const char *appName, int reload)
{
    PyObject *module, *app = NULL;
    
    char * p = (char *)strrchr(moduleName, '/');
    if (p)
    {
        *p = 0;
        chdir(moduleName);
        *p = '/';
    }

    module = PyDict_GetItemString(modict, moduleName);
    if (module != NULL)
    {
        if (reload)
        {
            Py_DECREF(module);
            module = NULL;
            PyDict_DelItemString(modict, moduleName);
        }
        else
        {
            Py_INCREF(module);
        }
    }
    if (!module)
        module = wsgiLoadModule(moduleName);
    if (module != NULL)
    {
        PyDict_SetItemString(modict, moduleName, module);
        if ( appName == NULL )
            appName = "application";
        app = PyObject_GetAttrString(module, (char *) appName);

        Py_XDECREF(module);

        if (app == NULL || !PyCallable_Check(app))
        {
            LOG_ERR("wsgiHandler getApp [%s] failed, pApp=%p.\n", appName,
                    app);
            if (app)
            {
                Py_XDECREF(app);
                app = NULL;
            }
        }
    }
    
    return app;
}


//The global modict
static PyObject *modict = NULL;


PyObject *preload_module(const char *moduleName, const char *appName, const char *progName)
{
    PyObject *app = NULL;
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
    if (wsgiSubInit(progName) != 0)
    {
        LOG_ERR("wsgiHandler wsgiSubInit ERROR.\n");
    }
    else
    {
        modict = PyDict_New();

        app = wsgiInitModule(modict, moduleName, appName, 0);
    }
    PyGILState_Release(gstate);
    return app;
}


int wsgiHandler(void *ctxt, char *moduleName, char *appName,
                const char *progName)
{
    PyGILState_STATE gstate;
    PyObject *ctxt_c_obj, *args, *start_resp;
    Request *req_obj = NULL;
    PyObject *result;
    int renew = 0;
    InterpreterObject *interp = NULL;
    const char *app_group_name = NULL;
    char *reloading = NULL;
    int reload = 0;
    PyObject *pApp;

    //a pointer to global or sub
    PyObject **pmodict = NULL;

    app_group_name = LSAPI_GetEnv_r((LSAPI_Request *)ctxt, "WSGIApplicationGroup");
#ifdef __DEBUG__
    //LOG_ERR("lsapi_wsgi: interpreter '%s'.\n", app_group_name);
#endif

    if (!app_group_name || strcasecmp(app_group_name, "global") == 0)
    {
        pmodict = &modict;
        gstate = PyGILState_Ensure();
    }
    else
    {
        interp = wsgi_acquire_interpreter(app_group_name, &renew);
        if (!interp)
        {
            LOG_ERR("wsgi_acquire_interpreter ERROR for name %s. \n", app_group_name);
            goto out;
        }
        pmodict = &interp->modict;
    }

    if (*pmodict == NULL)
    {
        if (wsgiSubInit(progName) != 0)
        {
            LOG_ERR("wsgiHandler wsgiSubInit ERROR.\n");
            goto out;
        }
        *pmodict = PyDict_New();
    }

    if (moduleName == NULL)
    {
        moduleName = LSAPI_GetScriptFileName_r((LSAPI_Request *) ctxt );
        appName = LSAPI_GetEnv_r((LSAPI_Request*)ctxt, "WSGICallableObject");
        if (moduleName == NULL )
        {
            LOG_ERR("wsgiLoadModule Error, moduleName is NULL.");
            goto out;
        }
    }
    
    if (renew == 0)
    {
        //setting "WSGIScriptReloading"
        reloading = LSAPI_GetEnv_r((LSAPI_Request*)ctxt, "WSGIScriptReloading");
        if (reloading != NULL && strcmp(reloading, "ON") == 0)
            reload = 1;
    }

    pApp = wsgiInitModule(*pmodict, moduleName, appName, reload);
    if (pApp == NULL)
        goto out;

    /* Create Request object, passing it the context as a CObject */
    ctxt_c_obj = PyCapsule_New(ctxt, "Request", NULL);
    if (ctxt_c_obj == NULL)
    {
        LOG_ERR("wsgiHandler failed to create Request object.\n");
        goto out;
    }

    args = Py_BuildValue("(O)", ctxt_c_obj);
    Py_DECREF(ctxt_c_obj);
    if (args == NULL)
    {
        LOG_ERR("wsgiHandler failed to build args.\n");
        goto out;
    }

    req_obj = (Request *) PyObject_CallObject((PyObject *) &Request_Type,
              args);
    Py_DECREF(args);
    if (req_obj == NULL)
    {
        LOG_ERR("wsgiHandler failed to create req_obj.\n");
        goto out;
    }
    wsgiSetRequestData(ctxt, req_obj);


    /* Get start_response callable */
    start_resp = PyObject_GetAttrString((PyObject *) req_obj,
                                        "start_response");
    if (start_resp == NULL)
    {
        LOG_ERR("wsgiHandler missing start_response.\n");
        goto out;
    }

    /* Build arguments and call application object */
    args = Py_BuildValue("(OO)", req_obj->environ, start_resp);
    Py_DECREF(start_resp);
    if (args == NULL)
    {
        LOG_ERR("wsgiHandler failed to build start_resp args.\n");
        goto out;
    }

    result = PyObject_CallObject(pApp, args);
    Py_DECREF(args);
    Py_XDECREF(pApp);

    if (result != NULL)
    {
        /* Handle the application response */
        req_obj->result = result;
        /* result now owned by req_obj */
        wsgiSendResponse(req_obj, result);    /* ignore return */
        wsgiCallClose(result);
    }
    else
        LOG_ERR("wsgiHandler pApp->start_response() return NULL.\n");
out:
    if (PyErr_Occurred())
    {
        PyErr_Print();

        /* Display HTTP 500 error, if possible */
        if (req_obj == NULL || !req_obj->headers_sent)
            sendResponse500(ctxt);
    }

    if (req_obj != NULL)
    {
        wsgiSetRequestData(ctxt, NULL);

        /* Don't rely on cyclic GC. Clear circular references NOW. */
        Request_clear(req_obj);

        Py_DECREF(req_obj);
    }

    if (interp)
        wsgi_release_interpreter(interp);
    else
        PyGILState_Release(gstate);

    /* Always return success. */
    return 0;
}


int wsgiAppHandler(void *ctxt, PyObject *app)
{
    PyObject *ctxt_c_obj, *args, *start_resp;
    Request *req_obj = NULL;
    PyObject *result;

    //LOG_ERR("wsgiAppHandler(%p)\n", app);

    if (app == NULL)
        goto out;

    /* Create Request object, passing it the context as a CObject */
    ctxt_c_obj = PyCapsule_New(ctxt, "Request", NULL);
    if (ctxt_c_obj == NULL)
    {
        LOG_ERR("wsgiAppHandler failed to create Request object.\n");
        goto out;
    }

    args = Py_BuildValue("(O)", ctxt_c_obj);
    Py_DECREF(ctxt_c_obj);
    if (args == NULL)
    {
        LOG_ERR("wsgiAppHandler failed to build args.\n");
        goto out;
    }
    //LOG_ERR("wsgiAppHandler args created.\n");

    req_obj = (Request *) PyObject_CallObject((PyObject *) &Request_Type,
              args);
    Py_DECREF(args);
    if (req_obj == NULL)
    {
        LOG_ERR("wsgiAppHandler failed to create req_obj.\n");
        goto out;
    }
    wsgiSetRequestData(ctxt, req_obj);

    //LOG_ERR("wsgiAppHandler create start_response.\n");

    /* Get start_response callable */
    start_resp = PyObject_GetAttrString((PyObject *) req_obj,
                                        "start_response");
    if (start_resp == NULL)
    {
        LOG_ERR("wsgiHandler missing start_response.\n");
        goto out;
    }

    /* Build arguments and call application object */
    args = Py_BuildValue("(OO)", req_obj->environ, start_resp);
    Py_DECREF(start_resp);
    if (args == NULL)
    {
        LOG_ERR("wsgiAppHandler failed to build start_resp args.\n");
        goto out;
    }

    //LOG_ERR("wsgiAppHandler call app->start_response.\n");

    result = PyObject_CallObject(app, args);
    Py_DECREF(args);

    if (result != NULL)
    {
        /* Handle the application response */
        req_obj->result = result;
        /* result now owned by req_obj */
        wsgiSendResponse(req_obj, result);    /* ignore return */
        wsgiCallClose(result);
    }
    else
        LOG_ERR("wsgiAppHandler pApp->start_response() return NULL.\n");
out:
    if (PyErr_Occurred())
    {
        PyErr_Print();

        /* Display HTTP 500 error, if possible */
        if (req_obj == NULL || !req_obj->headers_sent)
            sendResponse500(ctxt);
    }

    if (req_obj != NULL)
    {
        wsgiSetRequestData(ctxt, NULL);

        /* Don't rely on cyclic GC. Clear circular references NOW. */
        Request_clear(req_obj);

        Py_DECREF(req_obj);
    }

    /* Always return success. */
    return 0;
}


