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


#include <lsapilib.h>

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <Python.h>

#include <libgen.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "wsgi.h"


#define LSAPI_WSGI_VERSION      "1.2.0"

/*
Usage:
    lsapi-wsgi [-a <socket address>] [-m <module>:<app>]
example:  lsapi-wsgi [-h]|[-v] [-a 127.0.0.1:8000] [-m /home/test/mypython.py:testApp]
*/

int main ( int argc, char *argv[] )
{
    //int count = 0;
    int ret;
    int fd = 0;
    char *progName = "";
    char *moduleName = NULL;
    char *appName = "application";
    int i = 0;

    //Check non-running command line parameters
    if (argc >= 2)
    {
        if (strcmp(argv[1] , "-h") ==0 || strcmp(argv[1] , "--help") ==0)
        {

            fprintf(stdout, "Usage: ./lswsgi [-s <socket address>] [-m <module>:<app>\n       ./lswsgi -h|-v\n"\
                    "example:  lswsgi -a 127.0.0.1:8000 -m /home/test/mypython.py:testApp\n");
            exit(1);
        }

        else if (strcmp(argv[1] , "-v") ==0 || strcmp(argv[1] , "--version") ==0)
        {
            fprintf(stdout, "lswsgi version %s\nCopyright (C) by LiteSpeed Tech inc.\n", LSAPI_WSGI_VERSION);
            exit (1);
        }
    }

    if ( ( progName = strdup ( basename ( argv[0] ) ) ) == NULL )
    {
        perror ( NULL );
        exit ( 2 );
    }

    //start to check running argv
    i = 1;
    while(i + 1 < argc)
    {
        //valid aparameter start with '-'
        if (argv[i][0] != '-')
        {
            fprintf(stdout, "lswsgi: invalid parameter \'%s\', bypassed.\n", argv[i]);
            i ++;
            continue;
        }

        switch(argv[i][1])
        {
        case 'a':
            fd = LSAPI_CreateListenSock ( argv[i + 1], 10 );
            if ( fd != 0 )
            {
                dup2(fd, 0);
                close(fd);
            }
            i += 2;
            break;

        case 'm':
            moduleName = strdup(argv[i + 1]);
            if (moduleName == NULL)
            {
                perror ( NULL );
                exit ( 2 );
            }

            appName = strchr(moduleName, ':');
            if (appName != NULL)
            {
                *appName++ = 0x00;
            }

            //all right
            i += 2;
            break;

        default:
            ++ i;
            break;
        }
    }


    //next step
    //Init python
    //
    //Py_OptimizeFlag = atoi( getenv( "PYTHON_OPTIMIZE" ) );
    //
    // python_home = getenv( "PYTHONHOME" );
    //        Py_SetPythonHome((char *)python_home);

    // PYTHON_EGG_CACHE, not sure about this, maybe later

    // move code from wsgi_init()
    //PyEval_InitThreads();
    //Py_Initialize();
    //argv[0] = ( char * ) progName;
    //PySys_SetArgv ( 1, argv ); /* make sure sys.argv gets set */


    /* Initialize Python/WSGI */
    //Now, do not need to pass progName
    //argv[1] is appName, lsapi_wsgi uses "application" if "callable_object" is not set
    //lsapi_wsgi load script with wsgi_load_source() wsgi_reload_required()  wsgi_module_name()
    // each script file is a module
    //  lookup module with PyIMport_GetModuleDiect(), PyDict_GetItemString()
    //  once load module dynamically, can be called from inside wsgiHandler() function
    //  instead of initization statically at the beginning.
    //  wsgiHandler() is equivalent to lsapi_wsgi wsgi_execute_script()

    wsgiInit();

    PyObject *app = NULL;
    PyGILState_STATE gstate;
    if (moduleName)
    {
        app = preload_module(moduleName, appName, progName);
        if (app)
        {
            gstate = PyGILState_Ensure();
        }
    }
    
    LSAPI_Init();

#if 1
    
    LSAPI_Init_Env_Parameters( NULL );
    
    while ( LSAPI_Prefork_Accept_r ( &g_req ) >= 0 )
#else
    while ( LSAPI_Accept_r( &req ) >= 0 )
#endif
    {
        //count++;
        //if ( count >= 10000 )
        //    break;
        if (app)
            ret = wsgiAppHandler(&g_req, app);
        else
            ret = wsgiHandler ( &g_req, moduleName, appName, progName );
        LSAPI_Finish();
        if ( ret != 0 )
            break;
    }
    if (app)
    {
        Py_XDECREF(app);
        PyGILState_Release(gstate);
    }
    wsgiCleanup ();

    return EXIT_SUCCESS;
}
