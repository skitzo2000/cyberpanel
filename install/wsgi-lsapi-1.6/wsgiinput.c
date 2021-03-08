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

#include <assert.h>

#include "wsgi-int.h"
#include "lsapilib.h"


static void InputStream_dealloc ( InputStream *self )
{
    PyObject *tmp;


    tmp = ( PyObject * ) self->request;
    self->request = NULL;
    Py_XDECREF ( tmp );

    Py_TYPE(self)->tp_free ( ( PyObject * ) self );
}


static PyObject *InputStream_new ( PyTypeObject *type, PyObject *args, PyObject *kwds )
{
    InputStream *self;

    self = ( InputStream * ) type->tp_alloc ( type, 0 );
    if ( self != NULL )
    {
        self->request = NULL;
    }

    return ( PyObject * ) self;
}


/* InputStream constructor. Expects to be passed the parent Request and
   the received Content-Length. */
static int InputStream_init ( InputStream *self, PyObject *args, PyObject *kwds )
{
    Request *request;
    int size;

    if ( !PyArg_ParseTuple ( args, "O!i", &Request_Type, &request, &size ) )
        return -1;

    Py_INCREF ( request );
    self->request = request;

    return 0;
}


/* read() implementation */
static PyObject *InputStream_read ( InputStream *self, PyObject *args )
{
    int size = -1, avail;
    PyObject * result;
    char * data;
    if ( !PyArg_ParseTuple ( args, "|i:read", &size ) )
        return NULL;

    avail = LSAPI_GetReqBodyRemain_r ( ( LSAPI_Request * ) self->request->context );
    if ( avail <= 0 )
        return PyBytes_FromString ( "" );
    if ( size > avail )
        size = avail;
    result = PyBytes_FromStringAndSize ( NULL, size );
    if ( result == NULL )
        return NULL;

    data = PyBytes_AS_STRING ( result );

    avail = LSAPI_ReadReqBody_r ( ( LSAPI_Request * ) self->request->context, data,
                                  size );
    if ( avail <= 0 )
    {
        Py_XDECREF ( result );
        return NULL;
    }
    if ( avail < size )
    {
        _PyBytes_Resize ( &result, avail );
    }
    return result;
}


/* readline() implementation. Supports "size" argument not required by
   WSGI spec (but now required by Python 2.5's cgi module) */
static PyObject *InputStream_readline ( InputStream *self, PyObject *args )
{
    int size = -1, gotLF, avail;
    PyObject * result;
    char * data;

    if ( !PyArg_ParseTuple ( args, "|i:readline", &size ) )
        return NULL;

    avail = LSAPI_GetReqBodyRemain_r ( ( LSAPI_Request * ) self->request->context );
    if ( avail <= 0 )
        return PyBytes_FromString ( "" );
    if ( size > avail )
        size = avail;
    result = PyBytes_FromStringAndSize ( NULL, size );
    if ( result == NULL )
        return NULL;

    data = PyBytes_AsString(result);
    avail = LSAPI_ReqBodyGetLine_r ( ( LSAPI_Request * ) self->request->context, data,
                                     size, &gotLF );
    if ( avail <= 0 )
    {
        Py_XDECREF ( result );
        return NULL;
    }
    if ( avail < size )
    {
        _PyBytes_Resize ( &result, avail );
    }
    return result;
}


/* readlines() implementation. Supports "hint" argument. */
static PyObject *InputStream_readlines ( InputStream *self, PyObject *args )
{
    int hint = 0, total = 0;
    PyObject *lines = NULL, *args2 = NULL, *line;
    int len, ret;

    if ( !PyArg_ParseTuple ( args, "|i:readlines", &hint ) )
        return NULL;

    if ( ( lines = PyList_New ( 0 ) ) == NULL )
        return NULL;

    if ( ( args2 = PyTuple_New ( 0 ) ) == NULL )
        goto bad;

    if ( ( line = InputStream_readline ( self, args2 ) ) == NULL )
        goto bad;

    while ( ( len = PyBytes_GET_SIZE ( line ) ) > 0 )
    {
        ret = PyList_Append ( lines, line );
        Py_DECREF ( line );
        if ( ret )
            goto bad;
        total += len;
        if ( hint > 0 && total >= hint )
            break;

        if ( ( line = InputStream_readline ( self, args2 ) ) == NULL )
            goto bad;
    }

    Py_DECREF ( line );
    Py_DECREF ( args2 );

    return lines;

bad:
    Py_XDECREF ( args2 );
    Py_XDECREF ( lines );
    return NULL;
}


/* __iter__() implementation. Simply returns self. */
static PyObject *InputStream_iter ( InputStream *self )
{
    Py_INCREF ( self );
    return ( PyObject * ) self;
}


/* next() implementation for iteration protocol support */
static PyObject *InputStream_iternext ( InputStream *self )
{
    PyObject *line, *args;

    if ( ( args = PyTuple_New ( 0 ) ) == NULL )
        return NULL;

    line = InputStream_readline ( self, args );
    Py_DECREF ( args );
    if ( line == NULL )
        return NULL;

    if ( PyBytes_GET_SIZE ( line ) == 0 )
    {
        Py_DECREF ( line );
        PyErr_Clear();
        return NULL;
    }

    return line;
}


static PyMethodDef InputStream_methods[] =
{
    { "read", ( PyCFunction ) InputStream_read, METH_VARARGS,
        "Read from this input stream" },
    { "readline", ( PyCFunction ) InputStream_readline, METH_VARARGS,
      "Read a line from this input stream" },
    { "readlines", ( PyCFunction ) InputStream_readlines, METH_VARARGS,
      "Read lines from this input stream" },
    { NULL }
};


#if PY_MAJOR_VERSION >=3
#  define Py_TPFLAGS_HAVE_ITER 0
#endif


PyTypeObject InputStream_Type =
{
    PyVarObject_HEAD_INIT(NULL, 0)
    "lsapi_wsgi.InputStream",    /*tp_name*/
    sizeof ( InputStream ),    /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    ( destructor ) InputStream_dealloc, /*tp_dealloc*/
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_ITER, /*tp_flags*/
    "wsgi.input implementation", /* tp_doc */
    0,                     /* tp_traverse */
    0,                     /* tp_clear */
    0,                     /* tp_richcompare */
    0,                     /* tp_weaklistoffset */
    ( getiterfunc ) InputStream_iter, /* tp_iter */
    ( iternextfunc ) InputStream_iternext, /* tp_iternext */
    InputStream_methods,       /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    ( initproc ) InputStream_init, /* tp_init */
    0,                         /* tp_alloc */
    InputStream_new,           /* tp_new */
};

