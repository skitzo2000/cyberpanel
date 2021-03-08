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

#ifndef APS_WSGI_INT_H
#define APS_WSGI_INT_H

#include <Python.h>

#include <inttypes.h>

#include "wsgi.h"

typedef struct
{
    PyObject_HEAD
    /* All fields are private */
    void *context;
    PyObject *environ;
    PyObject *input;
    int       statusCode;
    PyObject *headers;
    PyObject *result;
    int headers_sent;
} Request;


typedef struct
{
    PyObject_HEAD
    Request *request;
} InputStream;

typedef struct
{
    PyObject_HEAD
    PyObject *filelike;
    int blocksize;
} FileWrapper;

extern PyTypeObject Request_Type;
extern PyTypeObject InputStream_Type;
extern PyTypeObject FileWrapper_Type;


#define LSAPI_OK               0
#define LSAPI_UNKNOWN_ERROR   -1
#define LSAPI_MEMORY_ERROR    -2
#define LSAPI_SOCKET_ERROR    -3
#define LSAPI_BUFFER_ERROR    -4
#define LSAPI_PROTOCOL_ERROR  -5
#define LSAPI_FILE_ERROR      -6


/* Glue logic between WSGI code and transport code */

/* WSGI -> transport (to be implemented by glue) */
/* Return 0 for success, non-zero for error */
int wsgiPopulateEnviron(Request *self);
int wsgiSendHeaders(void *ctxt, int statusCode,
                    int headerCount, const char *headers[]);
int wsgiSendBody(void *ctxt, const uint8_t *data, size_t length);
int wsgiSendFile(void *ctxt, int fd);
int wsgiPrimeInput(void *ctxt);
int wsgiGetBody(void *ctxt);

/* Request-specific opaque data handling */
void wsgiSetRequestData(void *ctxt, void *data);
void *wsgiGetRequestData(void *ctxt);

/* Misc. */
int wsgiGetContentLength(void *ctxt);

/* Transport -> WSGI (implemented by wsgi.c and input.c) */
/* Returns 0 on success, -1 otherwise */
//int wsgiHandler ( void *ctxt );
int wsgiBodyHandler(void *ctxt, uint8_t *data, size_t length);
int wsgiBodyHandlerNC(void *ctxt, uint8_t *data, size_t length,
                      void *tofree);

/* Utilities available to glue */
int wsgiPutEnv(Request *req, const char *key, const char *value);
const char *wsgiUnquote(const char *s);

/* Externs available to glue */
extern PyObject *wsgiStderr;
extern const char *wsgiScriptName;
extern int wsgiScriptNameLen;

#endif /* APS_WSGI_INT_H */
