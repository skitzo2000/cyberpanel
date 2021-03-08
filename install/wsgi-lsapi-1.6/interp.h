/*
Copyright (c) 2005, Lite Speed Technologies Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of the Lite Speed Technologies Inc nor the
      names of its contributors may be used to endorse or promote
      products derived from this software without specific prior
      written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/***************************************************************************
                          interp.h  -  description
                             -------------------
    begin                : Feb 6 2013
    copyright            : (C) 2005 - 2013 by George Wang
    email                : gwang@litespeedtech.com
 ***************************************************************************/


#ifndef  _INTERP_H_
#define  _INTERP_H_

#include "Python.h"

#define __DEBUG__

typedef struct {
    PyObject_HEAD
    char *name;
    PyInterpreterState *interp;
    PyThreadState *tstate;
    PyObject *modict; //contain all modules files which were already loaded
} InterpreterObject;

static PyTypeObject Interpreter_Type;


static InterpreterObject *newInterpreterObject(const char *name)
{
    InterpreterObject *self = NULL;
    PyThreadState *tstate = NULL;
    PyThreadState *save_tstate = NULL;
    PyObject *module = NULL;
    PyObject *object = NULL;
    PyObject *item = NULL;

    /* Create handle for interpreter and local data. */

    self = PyObject_New(InterpreterObject, &Interpreter_Type);
    if (self == NULL)
        return NULL;

    self->modict = NULL;

    assert(name != NULL);
    self->name = strdup(name);

    /*
     * Remember active thread state so can restore
     * it. This is actually the thread state
     * associated with simplified GIL state API.
     */

    save_tstate = PyThreadState_Swap(NULL);

    /*
     * Create the interpreter. If creation of the
     * interpreter fails it will restore the
     * existing active thread state for us so don't
     * need to worry about it in that case.
     */

    tstate = Py_NewInterpreter();

    if (!tstate) {
        PyErr_SetString(PyExc_RuntimeError, "Py_NewInterpreter() failed");

        Py_DECREF(self);

        return NULL;
    }

#ifdef __DEBUG__
    //fprintf( stderr, "lsapi_wsgi (pid=%d): Create interpreter '%s'.\n",
    //         getpid(), name);
#endif

    self->interp = tstate->interp;
    
    //object = newLogObject(NULL, APLOG_ERR, "stderr");
    //PySys_SetObject("stderr", object);
    //Py_DECREF(object);
    
    object = PyList_New(0);
#if PY_MAJOR_VERSION >= 3
    item = PyUnicode_FromString("lsapi_wsgi");
#else
    item = PyString_FromString("lsapi_wsgi");
#endif
    PyList_Append(object, item);
    PySys_SetObject("argv", object);
    Py_DECREF(item);
    Py_DECREF(object);

    /*
     * Force loading of codecs into interpreter. This has to be
     * done as not otherwise done in sub interpreters and if not
     * done, code running in sub interpreters can fail on some
     * platforms if a unicode string is added in sys.path and an
     * import then done.
     */

    item = PyCodec_Encoder("ascii");
    Py_XDECREF(item);

    /*
     * Create 'lsapi_wsgi' Python module. We first try and import an
     * external Python module of the same name. The intent is
     * that this external module would provide optional features
     * implementable using pure Python code. Don't want to
     * include them in the main Apache lsapi_wsgi package as that
     * complicates that package and also wouldn't allow them to
     * be released to a separate schedule. It is easier for
     * people to replace Python modules package with a new
     * version than it is to replace Apache module package.
     */

    module = PyImport_ImportModule("lsapi_wsgi");

    if (!module) {
        PyObject *modules = NULL;

        modules = PyImport_GetModuleDict();
        module = PyDict_GetItemString(modules, "lsapi_wsgi");

        if (module) {
            PyErr_Print();

            PyDict_DelItemString(modules, "lsapi_wsgi");
        }

        PyErr_Clear();

        module = PyImport_AddModule("lsapi_wsgi");

        Py_INCREF(module);
    }
    else if (!*name) {

#ifdef __DEBUG__
    //    fprintf(stderr,
    //            "lsapi_wsgi (pid=%d): Imported 'lsapi_wsgi'.",
    //            getpid());
#endif
    }

    /*
     * Add information about process group and application
     * group to the Python 'lsapi_wsgi' module.
     */

#if PY_MAJOR_VERSION >= 3
    PyModule_AddObject(module, "application_group",
                       PyUnicode_DecodeLatin1(name, strlen(name), NULL));
#else
    PyModule_AddObject(module, "application_group",
                       PyString_FromString(name));
#endif

    Py_DECREF(module);

    /*
     * Restore previous thread state. Only need to do
     * this where had to create a new interpreter. This
     * is basically anything except the first Python
     * interpreter instance. We need to restore it in
     * these cases as came into the function holding the
     * simplified GIL state for this thread but creating
     * the interpreter has resulted in a new thread
     * state object being created bound to the newly
     * created interpreter. In doing this though we want
     * to cache the thread state object which has been
     * created when interpreter is created. This is so
     * it can be reused later ensuring that thread local
     * data persists between requests.
     */

    self->tstate = tstate;
    PyThreadState_Swap(save_tstate);

    return self;
}


static void Interpreter_dealloc(InterpreterObject *self)
{
    PyThreadState *tstate = NULL;
    PyObject *exitfunc = NULL;
    PyObject *module = NULL;

    PyThreadState *tstate_enter = NULL;

    /*
     * We should always enter here with the Python GIL
     * held and an active thread state. This should only
     * now occur when shutting down interpreter and not
     * when releasing interpreter as don't support
     * recyling of interpreters within the process. Thus
     * the thread state should be that for the main
     * Python interpreter. Where dealing with a named
     * sub interpreter, we need to change the thread
     * state to that which was originally used to create
     * that sub interpreter before doing anything.
     */

    tstate_enter = PyThreadState_Get();

    if (*self->name) {
        tstate = self->tstate;

        /*
         * Swap to interpreter thread state that was used when
         * the sub interpreter was created.
         */

        PyThreadState_Swap(tstate);
    }

#ifdef __DEBUG__
    //fprintf(stderr,
    //        "lsapi_wsgi (pid=%d): Destroy interpreter '%s'.",
    //        getpid(), self->name);
#endif

    /*
     * Because the thread state we are using was created outside
     * of any Python code and is not the same as the Python main
     * thread, there is no record of it within the 'threading'
     * module. We thus need to access current thread function of
     * the 'threading' module to force it to create a thread
     * handle for the thread. If we do not do this, then the
     * 'threading' modules exit function will always fail
     * because it will not be able to find a handle for this
     * thread.
     */

    module = PyImport_ImportModule("threading");

    if (!module)
        PyErr_Clear();

    if (module) {
        PyObject *dict = NULL;
        PyObject *func = NULL;

        dict = PyModule_GetDict(module);
#if PY_MAJOR_VERSION >= 3
        func = PyDict_GetItemString(dict, "current_thread");
#else
        func = PyDict_GetItemString(dict, "currentThread");
#endif
        if (func) {
            PyObject *res = NULL;
            Py_INCREF(func);
            res = PyEval_CallObject(func, (PyObject *)NULL);
            if (!res) {
                PyErr_Clear();
            }
            Py_XDECREF(res);
            Py_DECREF(func);
        }
    }

    /*
     * In Python 2.5.1 an exit function is no longer used to
     * shutdown and wait on non daemon threads which were created
     * from Python code. Instead, in Py_Main() it explicitly
     * calls 'threading._shutdown()'. Thus need to emulate this
     * behaviour for those versions.
     */

    if (module) {
        PyObject *dict = NULL;
        PyObject *func = NULL;

        dict = PyModule_GetDict(module);
        func = PyDict_GetItemString(dict, "_shutdown");
        if (func) {
            PyObject *res = NULL;
            Py_INCREF(func);
            res = PyEval_CallObject(func, (PyObject *)NULL);

            Py_XDECREF(res);
            Py_DECREF(func);
        }
    }

    /* Finally done with 'threading' module. */

    Py_XDECREF(module);

    /*
     * Invoke exit functions by calling sys.exitfunc() for
     * Python 2.X and atexit._run_exitfuncs() for Python 3.X.
     * Note that in Python 3.X we can't call this on main Python
     * interpreter as for Python 3.X it doesn't deregister
     * functions as called, so have no choice but to rely on
     * Py_Finalize() to do it for the main interpreter. Now
     * that simplified GIL state API usage sorted out, this
     * should be okay.
     */

    module = NULL;

#if PY_MAJOR_VERSION >= 3

    module = PyImport_ImportModule("atexit");

    if (module) {
        PyObject *dict = NULL;

        dict = PyModule_GetDict(module);
        exitfunc = PyDict_GetItemString(dict, "_run_exitfuncs");
    }
    else
        PyErr_Clear();

#else
    exitfunc = PySys_GetObject("exitfunc");
#endif

    if (exitfunc) {
        PyObject *res = NULL;
        Py_INCREF(exitfunc);
        PySys_SetObject("exitfunc", (PyObject *)NULL);
        res = PyEval_CallObject(exitfunc, (PyObject *)NULL);

        Py_XDECREF(res);
        Py_DECREF(exitfunc);
    }

    Py_XDECREF(module);

    /* If we own it, we destroy it. */

    /*
     * We need to destroy all the thread state objects
     * associated with the interpreter. If there are
     * background threads that were created then this
     * may well cause them to crash the next time they
     * try to run. Only saving grace is that we are
     * trying to shutdown the process.
     */

    PyThreadState *tstate_save = tstate;
    PyThreadState *tstate_next = NULL;

    PyThreadState_Swap(NULL);

    tstate = PyInterpreterState_ThreadHead(tstate->interp);
    while (tstate) {
        tstate_next = PyThreadState_Next(tstate);
        if (tstate != tstate_save) {
            PyThreadState_Swap(tstate);
            PyThreadState_Clear(tstate);
            PyThreadState_Swap(NULL);
            PyThreadState_Delete(tstate);
        }
        tstate = tstate_next;
    }

    tstate = tstate_save;

    PyThreadState_Swap(tstate);

    /* Can now destroy the interpreter. */

    Py_EndInterpreter(tstate);

    PyThreadState_Swap(tstate_enter);

    free(self->name);

    PyObject_Del(self);
}


static PyTypeObject Interpreter_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "lsapi_wsgi.Interpreter",  /*tp_name*/
    sizeof(InterpreterObject), /*tp_basicsize*/
    0,                      /*tp_itemsize*/
    /* methods */
    (destructor)Interpreter_dealloc, /*tp_dealloc*/
    0,                      /*tp_print*/
    0,                      /*tp_getattr*/
    0,                      /*tp_setattr*/
    0,                      /*tp_compare*/
    0,                      /*tp_repr*/
    0,                      /*tp_as_number*/
    0,                      /*tp_as_sequence*/
    0,                      /*tp_as_mapping*/
    0,                      /*tp_hash*/
    0,                      /*tp_call*/
    0,                      /*tp_str*/
    0,                      /*tp_getattro*/
    0,                      /*tp_setattro*/
    0,                      /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,     /*tp_flags*/
    0,                      /*tp_doc*/
    0,                      /*tp_traverse*/
    0,                      /*tp_clear*/
    0,                      /*tp_richcompare*/
    0,                      /*tp_weaklistoffset*/
    0,                      /*tp_iter*/
    0,                      /*tp_iternext*/
    0,                      /*tp_methods*/
    0,                      /*tp_members*/
    0,                      /*tp_getset*/
    0,                      /*tp_base*/
    0,                      /*tp_dict*/
    0,                      /*tp_descr_get*/
    0,                      /*tp_descr_set*/
    0,                      /*tp_dictoffset*/
    0,                      /*tp_init*/
    0,                      /*tp_alloc*/
    0,                      /*tp_new*/
    0,                      /*tp_free*/
    0,                      /*tp_is_gc*/
};


/*
 * Startup and shutdown of Python interpreter. In lsapi_wsgi if
 * the Python interpreter hasn't been initialised by another
 * Apache module such as mod_python, we will take control and
 * initialise it. Need to remember that we initialised Python
 * and whether done in parent or child process as when done in
 * the parent we also take responsibility for performing special
 * Python fixups after Apache is forked and child process has
 * run.
 *
 * Note that by default we now defer initialisation of Python
 * until after the fork of processes as Python 3.X by design
 * doesn't clean up properly when it is destroyed causing
 * significant memory leaks into Apache parent process on an
 * Apache restart. Some Python 2.X versions also have real
 * memory leaks but not near as much. The result of deferring
 * initialisation is that can't benefit from copy on write
 * semantics for loaded data across a fork. Each process will
 * therefore have higher memory requirement where Python needs
 * to be used.
 */


static PyObject *wsgi_interpreters = NULL;


//Output: if renew is 1, it is new, need to reload the env
static InterpreterObject *wsgi_acquire_interpreter(const char *name, int *renew)
{
    PyThreadState *tstate = NULL;
    InterpreterObject *handle = NULL;

    PyGILState_STATE state;

    state = PyGILState_Ensure();

    handle = (InterpreterObject *)PyDict_GetItemString(wsgi_interpreters,
             name);
    if (!handle)
    {
        handle = newInterpreterObject(name);
        *renew = 1;

        if (!handle) {
            //ap_log_error(APLOG_MARK, WSGI_LOG_CRIT(0), wsgi_server,
            //             "lsapi_wsgi (pid=%d): Cannot create interpreter '%s'.",
            //             getpid(), name);

            PyGILState_Release(state);
            return NULL;
        }

        PyDict_SetItemString(wsgi_interpreters, name, (PyObject *)handle);
    }
    else
    {
        Py_INCREF(handle);
        *renew = 0;
    }

    PyGILState_Release(state);

    if (*name) {
        tstate = handle->tstate;
        PyEval_AcquireThread(tstate);
    }
    else {
        PyGILState_Ensure();

        tstate = PyThreadState_Get();
        if (tstate && tstate->gilstate_counter == 1)
            tstate->gilstate_counter++;
    }

    return handle;
}


static void wsgi_release_interpreter(InterpreterObject *handle)
{
    PyThreadState *tstate = NULL;

    PyGILState_STATE state;

    if (*handle->name) {
        tstate = PyThreadState_Get();
        PyEval_ReleaseThread(tstate);
    }
    else
        PyGILState_Release(PyGILState_UNLOCKED);


    state = PyGILState_Ensure();

    Py_DECREF(handle);

    PyGILState_Release(state);
}

#endif

