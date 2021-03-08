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

#include <Python.h>

#include <assert.h>
#include <sys/stat.h>
#include "lsapilib.h"
#include "wsgi-int.h"

int unquoteURL = 1;
int multiprocess = 1;

static int add_variable(const char *pKey, int keyLen, const char *pValue,
                        int valLen, void *arg)
{
    if (keyLen == 15 && strncmp(pKey, "SCRIPT_FILENAME", 15) == 0)
        return 1;

    wsgiPutEnv((Request *) arg, pKey, pValue);
    if (strcmp(pKey, "HTTPS") == 0)
        wsgiPutEnv((Request *) arg, "wsgi.url_scheme", "https");
    return 1;
}


int wsgiPopulateEnviron(Request *self)
{
    LSAPI_Request *req = (LSAPI_Request *) self->context;
    PyObject *environ = self->environ;
    PyObject *val;
    const char *req_uri = NULL;
    int ret;
    int result = -1;

    /*
      if (unquoteURL) {
        if ((req_uri = wsgiUnquote(req->uri)) == NULL) {
          PyErr_NoMemory();
          return -1;
        }
      }
    */

    wsgiPutEnv(self, "wsgi.url_scheme", "http");

    wsgiPutEnv(self, "PATH_INFO", LSAPI_GetScriptName_r(req));

    /* HTTP headers */
    LSAPI_ForeachHeader_r(req, add_variable, self);

    /* Attributes */
    LSAPI_ForeachEnv_r(req, add_variable, self);


    /* wsgi.version */
    if ((val = Py_BuildValue("(ii)", 1, 0)) == NULL)
        goto bad;
    ret = PyDict_SetItemString(environ, "wsgi.version", val);
    Py_DECREF(val);
    if (ret)
        goto bad;

    /* wsgi.input */
    if (PyDict_SetItemString(environ, "wsgi.input", self->input))
        goto bad;

    /* wsgi.errors */
    if (PyDict_SetItemString(environ, "wsgi.errors", wsgiStderr))
        goto bad;

    /* wsgi.multithread */
    if (PyDict_SetItemString(environ, "wsgi.multithread",
                             multiprocess ? Py_False : Py_True))
        goto bad;

    /* wsgi.multiprocess */
    if (PyDict_SetItemString(environ, "wsgi.multiprocess",
                             multiprocess ? Py_True : Py_False))
        goto bad;

    /* wsgi.run_once */
    if (PyDict_SetItemString(environ, "wsgi.run_once", Py_False))
        goto bad;

    /* wsgi.url_scheme */
    if (!PyDict_GetItemString(environ, "wsgi.url_scheme"))
    {
        if (wsgiPutEnv(self, "wsgi.url_scheme", "http"))
            goto bad;
    }
    if (PyDict_SetItemString(environ, "wsgi.file_wrapper",
                             (PyObject *) &FileWrapper_Type))
        goto bad;

    result = 0;

bad:
    if (req_uri)
        PyMem_Free((void *) req_uri);
    return result;
}


static int _wsgiSetError(int error)
{
    assert(error < 0);
    switch (error)
    {
    case LSAPI_MEMORY_ERROR:
        PyErr_SetString(PyExc_MemoryError, "LSAPI: Out of memory");
        break;
    case LSAPI_SOCKET_ERROR:
        PyErr_SetString(PyExc_IOError, "LSAPI: Socket read/write error");
        break;
    case LSAPI_BUFFER_ERROR:
        PyErr_SetString(PyExc_IndexError,
                        "LSAPI: Buffer overrun (protocol error)");
        break;
    case LSAPI_PROTOCOL_ERROR:
        PyErr_SetString(PyExc_ValueError, "LSAPI: Protocol error");
        break;
    case LSAPI_FILE_ERROR:
        PyErr_SetString(PyExc_IOError, "LSAPI: File error");
        break;
    default:
        PyErr_SetString(PyExc_RuntimeError, "LSAPI: Unknown error");
        break;
    }
    return -1;
}


int wsgiSendHeaders(void *ctxt, int statusCode,
                    int headerCount, const char *headers[])
{
    int i, j;
    LSAPI_Request *req = (LSAPI_Request *) ctxt;
    int haserror = 0;

    Py_BEGIN_ALLOW_THREADS
    LSAPI_SetRespStatus_r(req, statusCode);

    for (i = 0; i < headerCount; i++)
    {
        j = i << 1;
        if (LSAPI_AppendRespHeader2_r(req, headers[j], headers[j + 1]) == -1)
        {
            haserror = 1;
            break;
        }
    }
    Py_END_ALLOW_THREADS

    if (haserror == 1)
        return _wsgiSetError(LSAPI_SOCKET_ERROR);
    else
        return 0;
}


int wsgiSendBody(void *ctxt, const uint8_t *data, size_t len)
{
    int ret = 0;

    Py_BEGIN_ALLOW_THREADS
    ret  = LSAPI_Write_r((LSAPI_Request *) ctxt, (const char *) data, len);
    Py_END_ALLOW_THREADS

    if (ret < (int)len)
        return _wsgiSetError(LSAPI_SOCKET_ERROR);
    else
        return 0;
}


int wsgiSendFile(void *ctxt, int fd)
{
    int ret;
    off_t offset = 0;
    size_t size = 0;
    struct stat statbuf;
    if (fstat(fd, &statbuf) != 0)
        return -1;

    size = statbuf.st_size - offset;

    Py_BEGIN_ALLOW_THREADS
    ret = LSAPI_sendfile_r((LSAPI_Request *) ctxt, fd, &offset, size);
    Py_END_ALLOW_THREADS

    if (ret < (int)size)
        return _wsgiSetError(LSAPI_FILE_ERROR);
    else
        return 0;
}


void wsgiSetRequestData(void *ctxt, void *data)
{
    LSAPI_SetAppData_r((LSAPI_Request *) ctxt, data);
}


void *wsgiGetRequestData(void *ctxt)
{
    return LSAPI_GetAppData_r((LSAPI_Request *) ctxt);
}


int wsgiGetContentLength(void *ctxt)
{
    return LSAPI_GetReqBodyLen_r((LSAPI_Request *) ctxt);
}
