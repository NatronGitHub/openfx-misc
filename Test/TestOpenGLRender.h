/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2016 INRIA
 *
 * openfx-misc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX TestOpenGL plugin.
 */

#include "TestOpenGL.h"

#include <cstring> // strstr, strchr, strlen
#include <cstdio> // sscanf, vsnprintf, fwrite
#include <cassert>

#include "ofxsMacros.h"
#include "ofxsLog.h"
#include "ofxsCoords.h"

// first, check that the file is used in a good way
#if !defined(USE_OPENGL) && !defined(USE_OSMESA)
#error "USE_OPENGL or USE_OSMESA must be defined before including this file."
#endif
#if defined(USE_OPENGL) && defined(USE_OSMESA)
#error "include this file first only once, either with USE_OPENGL, or with USE_OSMESA"
#endif

#if defined(USE_OSMESA) || !( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) )
#define GL_GLEXT_PROTOTYPES
#endif

#ifdef USE_OSMESA
#  include <GL/gl_mangle.h>
#  include <GL/glu_mangle.h>
#  include <GL/osmesa.h>
#  include "ofxsMultiThread.h"
#  define RENDERFUNC renderMesa
#  define contextAttached contextAttachedMesa
#  define contextDetached contextDetachedMesa
#else
#  define RENDERFUNC renderGL
#endif

#ifdef __APPLE__
#  include <OpenGL/gl.h>
#  include <OpenGL/glext.h>
//#  include <OpenGL/glu.h>
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
//#  include <GL/glu.h>
#endif

#ifndef DEBUG
#define DPRINT(args) (void)0
#define glCheckError() ( (void)0 )
#else
#include <cstdarg> // ...
#include <iostream>
#include <stdio.h> // for snprintf & _snprintf
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  include <windows.h>
#  if defined(_MSC_VER) && _MSC_VER < 1900
#    define snprintf _snprintf
#  endif
#endif // defined(_WIN32) || defined(__WIN32__) || defined(WIN32)


#if !defined(USE_OSMESA) && ( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) )
// Program
#ifdef GL_VERSION_2_0
// static PFNGLCREATEPROGRAMPROC glCreateProgram = NULL;
// static PFNGLDELETEPROGRAMPROC glDeleteProgram = NULL;
// static PFNGLUSEPROGRAMPROC glUseProgram = NULL;
// static PFNGLATTACHSHADERPROC glAttachShader = NULL;
// static PFNGLDETACHSHADERPROC glDetachShader = NULL;
// static PFNGLLINKPROGRAMPROC glLinkProgram = NULL;
// static PFNGLGETPROGRAMIVPROC glGetProgramiv = NULL;
// static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = NULL;
// static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = NULL;
// static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
// static PFNGLUNIFORM1IPROC glUniform1i = NULL;
// static PFNGLUNIFORM2IPROC glUniform2i = NULL;
// static PFNGLUNIFORM3IPROC glUniform3i = NULL;
// static PFNGLUNIFORM4IPROC glUniform4i = NULL;
// static PFNGLUNIFORM1IVPROC glUniform1iv = NULL;
// static PFNGLUNIFORM2IVPROC glUniform2iv = NULL;
// static PFNGLUNIFORM3IVPROC glUniform3iv = NULL;
// static PFNGLUNIFORM4IVPROC glUniform4iv = NULL;
// static PFNGLUNIFORM1FPROC glUniform1f = NULL;
// static PFNGLUNIFORM2FPROC glUniform2f = NULL;
// static PFNGLUNIFORM3FPROC glUniform3f = NULL;
// static PFNGLUNIFORM4FPROC glUniform4f = NULL;
// static PFNGLUNIFORM1FVPROC glUniform1fv = NULL;
// static PFNGLUNIFORM2FVPROC glUniform2fv = NULL;
// static PFNGLUNIFORM3FVPROC glUniform3fv = NULL;
// static PFNGLUNIFORM4FVPROC glUniform4fv = NULL;
// static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = NULL;
// static PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation = NULL;
// static PFNGLVERTEXATTRIB1FPROC glVertexAttrib1f = NULL;
// static PFNGLVERTEXATTRIB1FVPROC glVertexAttrib1fv = NULL;
// static PFNGLVERTEXATTRIB2FVPROC glVertexAttrib2fv = NULL;
// static PFNGLVERTEXATTRIB3FVPROC glVertexAttrib3fv = NULL;
// static PFNGLVERTEXATTRIB4FVPROC glVertexAttrib4fv = NULL;
// static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = NULL;
// static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = NULL;
// static PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = NULL;
// static PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation = NULL;
// static PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform = NULL;
#endif

// Shader
#ifdef GL_VERSION_2_0
// static PFNGLCREATESHADERPROC glCreateShader = NULL;
// static PFNGLDELETESHADERPROC glDeleteShader = NULL;
// static PFNGLSHADERSOURCEPROC glShaderSource = NULL;
// static PFNGLCOMPILESHADERPROC glCompileShader = NULL;
// static PFNGLGETSHADERIVPROC glGetShaderiv = NULL;
#endif

// VBO
#ifdef GL_VERSION_1_5
// static PFNGLGENBUFFERSPROC glGenBuffers = NULL;
// static PFNGLBINDBUFFERPROC glBindBuffer = NULL;
// static PFNGLBUFFERDATAPROC glBufferData = NULL;
#endif

//Multitexture
#ifdef GL_VERSION_1_3
// static PFNGLACTIVETEXTUREARBPROC glActiveTexture = NULL;
#endif
#ifdef GL_VERSION_1_3_DEPRECATED
// static PFNGLCLIENTACTIVETEXTUREPROC glClientActiveTexture = NULL;
// static PFNGLMULTITEXCOORD2FPROC glMultiTexCoord2f = NULL;
#endif

#ifdef GL_ARB_framebuffer_object
// Framebuffers
// static PFNGLISFRAMEBUFFERPROC glIsFramebuffer = NULL;
// static PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = NULL;
// static PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = NULL;
// static PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = NULL;
// static PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = NULL;
// static PFNGLFRAMEBUFFERTEXTURE1DPROC glFramebufferTexture1D = NULL;
// static PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = NULL;
// static PFNGLFRAMEBUFFERTEXTURE3DPROC glFramebufferTexture3D = NULL;
// static PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer = NULL;
// static PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC glGetFramebufferAttachmentParameteriv = NULL;
static PFNGLGENERATEMIPMAPPROC glGenerateMipmap = NULL;
#endif

#ifdef GL_ARB_sync
// Sync Objects https://www.opengl.org/wiki/Sync_Object
// static PFNGLFENCESYNCPROC glFenceSync = NULL;
// static PFNGLISSYNCPROC glIsSync = NULL;
// static PFNGLDELETESYNCPROC glDeleteSync = NULL;
// static PFNGLCLIENTWAITSYNCPROC glClientWaitSync = NULL;
// static PFNGLWAITSYNCPROC glWaitSync = NULL;
#endif

#endif // if !defined(USE_OSMESA) && ( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) )

using namespace OFX;

// put a breakpoint in glError to halt the debugger
inline static void
glError() {}

inline static const char*
glErrorString(GLenum errorCode)
{
    static const struct
    {
        GLenum code;
        const char *string;
    }

    errors[] =
    {
        /* GL */
        {GL_NO_ERROR, "no error"},
        {GL_INVALID_ENUM, "invalid enumerant"},
        {GL_INVALID_VALUE, "invalid value"},
        {GL_INVALID_OPERATION, "invalid operation"},
        {GL_STACK_OVERFLOW, "stack overflow"},
        {GL_STACK_UNDERFLOW, "stack underflow"},
        {GL_OUT_OF_MEMORY, "out of memory"},
#ifdef GL_EXT_histogram
        {GL_TABLE_TOO_LARGE, "table too large"},
#endif
#ifdef GL_EXT_framebuffer_object
        {GL_INVALID_FRAMEBUFFER_OPERATION_EXT, "invalid framebuffer operation"},
#endif

        {0, NULL }
    };
    int i;

    for (i = 0; errors[i].string; i++) {
        if (errors[i].code == errorCode) {
            return errors[i].string;
        }
    }

    return NULL;
}

#define glCheckError()                                                  \
    {                                                                   \
        GLenum _glerror_ = glGetError();                                \
        if (_glerror_ != GL_NO_ERROR) {                                 \
            std::cout << "GL_ERROR: " << __FILE__ << ":" << __LINE__ << " " << glErrorString(_glerror_) << std::endl; \
            glError();                                                  \
        }                                                               \
    }

#define DPRINT(args) print_dbg args
static
void
print_dbg(const char *format,
          ...)
{
    char str[1024];
    va_list ap;

    va_start(ap, format);
    size_t size = sizeof(str);
#if defined(_MSC_VER)
#  if _MSC_VER >= 1400
    vsnprintf_s(str, size, _TRUNCATE, format, ap);
#  else
    if (size == 0) {      /* not even room for a \0? */
        return -1;        /* not what C99 says to do, but what windows does */
    }
    str[size - 1] = '\0';
    _vsnprintf(str, size - 1, format, ap);
#  endif
#else
    vsnprintf(str, size, format, ap);
#endif
    std::fwrite(str, sizeof(char), std::strlen(str), stderr);
    //std::cout << str;
    std::fflush(stderr);
#ifdef _WIN32
    OutputDebugString(str);
#endif
    va_end(ap);
}

#endif // ifndef DEBUG

#ifdef USE_OSMESA
struct TestOpenGLPlugin::OSMesaPrivate
{
    OSMesaPrivate(TestOpenGLPlugin *effect)
        : _effect(effect)
        , _ctx(0)
        , _ctxFormat(0)
        , _ctxDepthBits(0)
        , _ctxStencilBits(0)
        , _ctxAccumBits(0)
    {
    }

    ~OSMesaPrivate()
    {
        /* destroy the context */
        if (_ctx) {
            // make the context current, with a dummy buffer
            unsigned char buffer[4];
            OSMesaMakeCurrent(_ctx, buffer, GL_UNSIGNED_BYTE, 1, 1);
            _effect->contextDetachedMesa(NULL);
            OSMesaMakeCurrent(_ctx, NULL, 0, 0, 0); // detach buffer from context
            OSMesaMakeCurrent(NULL, NULL, 0, 0, 0); // disactivate the context (not really recessary)
            OSMesaDestroyContext( _ctx );
            assert( !OSMesaGetCurrentContext() );
        }
    }

    void setContext(GLenum format,
                    GLint depthBits,
                    GLenum type,
                    GLint stencilBits,
                    GLint accumBits,
                    void* buffer,
                    const OfxRectI &dstBounds)
    {
        bool newContext = false;

        if (!buffer) {
            //printf("%p before OSMesaMakeCurrent(%p,buf=NULL), OSMesaGetCurrentContext=%p\n", pthread_self(), _ctx, OSMesaGetCurrentContext());
            OSMesaMakeCurrent(_ctx, NULL, 0, 0, 0);

            return;
        }
        if ( !_ctx || ( (format      != _ctxFormat) &&
                        ( depthBits  != _ctxDepthBits) &&
                        ( stencilBits != _ctxStencilBits) &&
                        ( accumBits  != _ctxAccumBits) ) ) {
            /* destroy the context */
            if (_ctx) {
                //printf("%p before OSMesaDestroyContext(%p), OSMesaGetCurrentContext=%p\n", pthread_self(), _ctx, OSMesaGetCurrentContext());
                // make the context current, with a dummy buffer
                unsigned char buffer[4];
                OSMesaMakeCurrent(_ctx, buffer, GL_UNSIGNED_BYTE, 1, 1);
                _effect->contextDetachedMesa(NULL);
                OSMesaMakeCurrent(_ctx, NULL, 0, 0, 0); // detach buffer from context
                OSMesaMakeCurrent(NULL, NULL, 0, 0, 0); // disactivate the context (not really recessary)
                OSMesaDestroyContext( _ctx );
                assert( !OSMesaGetCurrentContext() );
                _ctx = 0;
            }
            assert(!_ctx);

            /* Create an RGBA-mode context */
#if OSMESA_MAJOR_VERSION * 100 + OSMESA_MINOR_VERSION >= 305
            /* specify Z, stencil, accum sizes */
            _ctx = OSMesaCreateContextExt( format, depthBits, stencilBits, accumBits, NULL );
#else
            _ctx = OSMesaCreateContext( format, NULL );
#endif
            if (!_ctx) {
                DPRINT( ("OSMesaCreateContext failed!\n") );
                OFX::throwSuiteStatusException(kOfxStatFailed);

                return;
            }
            _ctxFormat = format;
            _ctxDepthBits = depthBits;
            _ctxStencilBits = stencilBits;
            _ctxAccumBits = accumBits;
            newContext = true;
        }
        // optional: enable Gallium postprocess filters
#if OSMESA_MAJOR_VERSION * 100 + OSMESA_MINOR_VERSION >= 1000
        //OSMesaPostprocess(_ctx, const char *filter, unsigned enable_value);
#endif

        //printf("%p before OSMesaMakeCurrent(%p), OSMesaGetCurrentContext=%p\n", pthread_self(), _ctx, OSMesaGetCurrentContext());

        /* Bind the buffer to the context and make it current */
        if ( !OSMesaMakeCurrent( _ctx, buffer, type, dstBounds.x2 - dstBounds.x1, dstBounds.y2 - dstBounds.y1 ) ) {
            DPRINT( ("OSMesaMakeCurrent failed!\n") );
            OFX::throwSuiteStatusException(kOfxStatFailed);

            return;
        }
        //OSMesaPixelStore(OSMESA_Y_UP, true); // default value
        //OSMesaPixelStore(OSMESA_ROW_LENGTH, dstBounds.x2 - dstBounds.x1); // default value
        if (newContext) {
            _effect->contextAttachedMesa(false);
        } else {
            // set viewport
            glViewport(0, 0, dstBounds.x2 - dstBounds.x1, dstBounds.y2 - dstBounds.y1);
        }
    } // setContext

    OSMesaContext ctx() { return _ctx; }

    TestOpenGLPlugin *_effect;
    // information about the current Mesa context
    OSMesaContext _ctx;
    GLenum _ctxFormat;
    GLint _ctxDepthBits;
    GLint _ctxStencilBits;
    GLint _ctxAccumBits;
    OpenGLContextData _openGLContextData;
};

void
TestOpenGLPlugin::initMesa()
{
}

void
TestOpenGLPlugin::exitMesa()
{
    AutoMutex lock( _osmesaMutex.get() );

    for (std::list<OSMesaPrivate *>::iterator it = _osmesa.begin(); it != _osmesa.end(); ++it) {
        delete *it;
    }
    _osmesa.clear();
}

#endif // ifdef USE_OSMESA

/* The OpenGL teapot */

/**
   (c) Copyright 1993, Silicon Graphics, Inc.

   ALL RIGHTS RESERVED

   Permission to use, copy, modify, and distribute this software
   for any purpose and without fee is hereby granted, provided
   that the above copyright notice appear in all copies and that
   both the copyright notice and this permission notice appear in
   supporting documentation, and that the name of Silicon
   Graphics, Inc. not be used in advertising or publicity
   pertaining to distribution of the software without specific,
   written prior permission.

   THE MATERIAL EMBODIED ON THIS SOFTWARE IS PROVIDED TO YOU
   "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, EXPRESS, IMPLIED OR
   OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  IN NO
   EVENT SHALL SILICON GRAPHICS, INC.  BE LIABLE TO YOU OR ANYONE
   ELSE FOR ANY DIRECT, SPECIAL, INCIDENTAL, INDIRECT OR
   CONSEQUENTIAL DAMAGES OF ANY KIND, OR ANY DAMAGES WHATSOEVER,
   INCLUDING WITHOUT LIMITATION, LOSS OF PROFIT, LOSS OF USE,
   SAVINGS OR REVENUE, OR THE CLAIMS OF THIRD PARTIES, WHETHER OR
   NOT SILICON GRAPHICS, INC.  HAS BEEN ADVISED OF THE POSSIBILITY
   OF SUCH LOSS, HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
   ARISING OUT OF OR IN CONNECTION WITH THE POSSESSION, USE OR
   PERFORMANCE OF THIS SOFTWARE.

   US Government Users Restricted Rights

   Use, duplication, or disclosure by the Government is subject to
   restrictions set forth in FAR 52.227.19(c)(2) or subparagraph
   (c)(1)(ii) of the Rights in Technical Data and Computer
   Software clause at DFARS 252.227-7013 and/or in similar or
   successor clauses in the FAR or the DOD or NASA FAR
   Supplement.  Unpublished-- rights reserved under the copyright
   laws of the United States.  Contractor/manufacturer is Silicon
   Graphics, Inc., 2011 N.  Shoreline Blvd., Mountain View, CA
   94039-7311.

   OpenGL(TM) is a trademark of Silicon Graphics, Inc.
 */

/* Rim, body, lid, and bottom data must be reflected in x and
   y; handle and spout data across the y axis only.  */

static int patchdata[][16] =
{
    /* rim */
    {102, 103, 104, 105, 4, 5, 6, 7, 8, 9, 10, 11,
     12, 13, 14, 15},
    /* body */
    {12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
     24, 25, 26, 27},
    {24, 25, 26, 27, 29, 30, 31, 32, 33, 34, 35, 36,
     37, 38, 39, 40},
    /* lid */
    {96, 96, 96, 96, 97, 98, 99, 100, 101, 101, 101,
     101, 0, 1, 2, 3, },
    {0, 1, 2, 3, 106, 107, 108, 109, 110, 111, 112,
     113, 114, 115, 116, 117},
    /* bottom */
    {118, 118, 118, 118, 124, 122, 119, 121, 123, 126,
     125, 120, 40, 39, 38, 37},
    /* handle */
    {41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
     53, 54, 55, 56},
    {53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64,
     28, 65, 66, 67},
    /* spout */
    {68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
     80, 81, 82, 83},
    {80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91,
     92, 93, 94, 95}
};
/* *INDENT-OFF* */

static float cpdata[][3] =
{
{0.2, 0, 2.7}, {0.2, -0.112, 2.7}, {0.112, -0.2, 2.7}, {0,
-0.2, 2.7}, {1.3375, 0, 2.53125}, {1.3375, -0.749, 2.53125},
{0.749, -1.3375, 2.53125}, {0, -1.3375, 2.53125}, {1.4375,
0, 2.53125}, {1.4375, -0.805, 2.53125}, {0.805, -1.4375,
2.53125}, {0, -1.4375, 2.53125}, {1.5, 0, 2.4}, {1.5, -0.84,
2.4}, {0.84, -1.5, 2.4}, {0, -1.5, 2.4}, {1.75, 0, 1.875},
{1.75, -0.98, 1.875}, {0.98, -1.75, 1.875}, {0, -1.75,
1.875}, {2, 0, 1.35}, {2, -1.12, 1.35}, {1.12, -2, 1.35},
{0, -2, 1.35}, {2, 0, 0.9}, {2, -1.12, 0.9}, {1.12, -2,
0.9}, {0, -2, 0.9}, {-2, 0, 0.9}, {2, 0, 0.45}, {2, -1.12,
0.45}, {1.12, -2, 0.45}, {0, -2, 0.45}, {1.5, 0, 0.225},
{1.5, -0.84, 0.225}, {0.84, -1.5, 0.225}, {0, -1.5, 0.225},
{1.5, 0, 0.15}, {1.5, -0.84, 0.15}, {0.84, -1.5, 0.15}, {0,
-1.5, 0.15}, {-1.6, 0, 2.025}, {-1.6, -0.3, 2.025}, {-1.5,
-0.3, 2.25}, {-1.5, 0, 2.25}, {-2.3, 0, 2.025}, {-2.3, -0.3,
2.025}, {-2.5, -0.3, 2.25}, {-2.5, 0, 2.25}, {-2.7, 0,
2.025}, {-2.7, -0.3, 2.025}, {-3, -0.3, 2.25}, {-3, 0,
2.25}, {-2.7, 0, 1.8}, {-2.7, -0.3, 1.8}, {-3, -0.3, 1.8},
{-3, 0, 1.8}, {-2.7, 0, 1.575}, {-2.7, -0.3, 1.575}, {-3,
-0.3, 1.35}, {-3, 0, 1.35}, {-2.5, 0, 1.125}, {-2.5, -0.3,
1.125}, {-2.65, -0.3, 0.9375}, {-2.65, 0, 0.9375}, {-2,
-0.3, 0.9}, {-1.9, -0.3, 0.6}, {-1.9, 0, 0.6}, {1.7, 0,
1.425}, {1.7, -0.66, 1.425}, {1.7, -0.66, 0.6}, {1.7, 0,
0.6}, {2.6, 0, 1.425}, {2.6, -0.66, 1.425}, {3.1, -0.66,
0.825}, {3.1, 0, 0.825}, {2.3, 0, 2.1}, {2.3, -0.25, 2.1},
{2.4, -0.25, 2.025}, {2.4, 0, 2.025}, {2.7, 0, 2.4}, {2.7,
-0.25, 2.4}, {3.3, -0.25, 2.4}, {3.3, 0, 2.4}, {2.8, 0,
2.475}, {2.8, -0.25, 2.475}, {3.525, -0.25, 2.49375},
{3.525, 0, 2.49375}, {2.9, 0, 2.475}, {2.9, -0.15, 2.475},
{3.45, -0.15, 2.5125}, {3.45, 0, 2.5125}, {2.8, 0, 2.4},
{2.8, -0.15, 2.4}, {3.2, -0.15, 2.4}, {3.2, 0, 2.4}, {0, 0,
3.15}, {0.8, 0, 3.15}, {0.8, -0.45, 3.15}, {0.45, -0.8,
3.15}, {0, -0.8, 3.15}, {0, 0, 2.85}, {1.4, 0, 2.4}, {1.4,
-0.784, 2.4}, {0.784, -1.4, 2.4}, {0, -1.4, 2.4}, {0.4, 0,
2.55}, {0.4, -0.224, 2.55}, {0.224, -0.4, 2.55}, {0, -0.4,
2.55}, {1.3, 0, 2.55}, {1.3, -0.728, 2.55}, {0.728, -1.3,
2.55}, {0, -1.3, 2.55}, {1.3, 0, 2.4}, {1.3, -0.728, 2.4},
{0.728, -1.3, 2.4}, {0, -1.3, 2.4}, {0, 0, 0}, {1.425,
-0.798, 0}, {1.5, 0, 0.075}, {1.425, 0, 0}, {0.798, -1.425,
0}, {0, -1.5, 0.075}, {0, -1.425, 0}, {1.5, -0.84, 0.075},
{0.84, -1.5, 0.075}
};

static float tex[2][2][2] =
{
{ {0, 0},
{1, 0}},
{ {0, 1},
{1, 1}}
};

/* *INDENT-ON* */

static void
teapot(GLint grid,
       GLdouble scale,
       GLenum type)
{
    float p[4][4][3], q[4][4][3], r[4][4][3], s[4][4][3];
    long i, j, k, l;

    glPushAttrib(GL_ENABLE_BIT | GL_EVAL_BIT);
    glEnable(GL_AUTO_NORMAL);
    glEnable(GL_NORMALIZE);
    glEnable(GL_MAP2_VERTEX_3);
    glEnable(GL_MAP2_TEXTURE_COORD_2);
    glPushMatrix();
    glRotatef(270.0, 1.0, 0.0, 0.0);
    glScalef(0.5 * scale, 0.5 * scale, 0.5 * scale);
    glTranslatef(0.0, 0.0, -1.5);
    for (i = 0; i < 10; i++) {
        for (j = 0; j < 4; j++) {
            for (k = 0; k < 4; k++) {
                for (l = 0; l < 3; l++) {
                    p[j][k][l] = cpdata[patchdata[i][j * 4 + k]][l];
                    q[j][k][l] = cpdata[patchdata[i][j * 4 + (3 - k)]][l];
                    if (l == 1) {
                        q[j][k][l] *= -1.0;
                    }
                    if (i < 6) {
                        r[j][k][l] =
                            cpdata[patchdata[i][j * 4 + (3 - k)]][l];
                        if (l == 0) {
                            r[j][k][l] *= -1.0;
                        }
                        s[j][k][l] = cpdata[patchdata[i][j * 4 + k]][l];
                        if (l == 0) {
                            s[j][k][l] *= -1.0;
                        }
                        if (l == 1) {
                            s[j][k][l] *= -1.0;
                        }
                    }
                }
            }
        }
        glMap2f(GL_MAP2_TEXTURE_COORD_2, 0, 1, 2, 2, 0, 1, 4, 2,
                &tex[0][0][0]);
        glMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 4, 0, 1, 12, 4,
                &p[0][0][0]);
        glMapGrid2f(grid, 0.0, 1.0, grid, 0.0, 1.0);
        glEvalMesh2(type, 0, grid, 0, grid);
        glMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 4, 0, 1, 12, 4,
                &q[0][0][0]);
        glEvalMesh2(type, 0, grid, 0, grid);
        if (i < 6) {
            glMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 4, 0, 1, 12, 4,
                    &r[0][0][0]);
            glEvalMesh2(type, 0, grid, 0, grid);
            glMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 4, 0, 1, 12, 4,
                    &s[0][0][0]);
            glEvalMesh2(type, 0, grid, 0, grid);
        }
    }
    glPopMatrix();
    glPopAttrib();
} // teapot

/* CENTRY */
static void
glutSolidTeapot(GLdouble scale)
{
    teapot(7, scale, GL_FILL);
}

//static void
//glutWireTeapot(GLdouble scale)
//{
//    teapot(10, scale, GL_LINE);
//}

/* ENDCENTRY */

static
int
glutExtensionSupported( const char* extension )
{
    const char *extensions, *start;
    const size_t len = std::strlen( extension );

    /* Make sure there is a current window, and thus a current context available */
    //FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutExtensionSupported" );
    //freeglut_return_val_if_fail( fgStructure.CurrentWindow != NULL, 0 );

    if ( std::strchr(extension, ' ') ) {
        return 0;
    }
    start = extensions = (const char *) glGetString(GL_EXTENSIONS);

    /* XXX consider printing a warning to stderr that there's no current
     * rendering context.
     */
    //freeglut_return_val_if_fail( extensions != NULL, 0 );
    if (extensions == NULL) {
        return 0;
    }

    while (1) {
        const char *p = std::strstr(extensions, extension);
        if (!p) {
            return 0;  /* not found */
        }
        /* check that the match isn't a super string */
        if ( ( (p == start) || (p[-1] == ' ') ) && ( (p[len] == ' ') || (p[len] == 0) ) ) {
            return 1;
        }
        /* skip the false match and continue */
        extensions = p + len;
    }

    return 0;
}

//#define RND_GL_STATE_DEBUG 1
OFXS_NAMESPACE_ANONYMOUS_ENTER

//data for debugging the GL state by dumping it's contents
#if RND_GL_STATE_DEBUG

#define FALSE false
#define TRUE true
#define sdword long
#define ubyte unsigned char
#define dbgAssert assert
#define dbgMessagef printf
#define colRealToUbyte(r) ( (ubyte)( (r) * 255.0f ) )

typedef struct
{
    const char *name;
    GLenum enumeration;
    bool bDefault;
}

enumentry;
#define enumEntry(string, n)            {string, n, FALSE}
#define enumDefaultEntry(string, n)     {string, n, TRUE}
#define enumEnd                         {NULL, 0, FALSE}
#define enumError                       {"ERROR", 0xffffff8, TRUE}
enumentry rndBoolEnums[] =
{
    enumEntry("GL_TRUE", GL_TRUE),
    enumDefaultEntry("GL_FALSE", GL_FALSE),
    enumEnd
};
enumentry rndAlphaTestFuncEnum[] =
{
    enumEntry("GL_NEVER", GL_NEVER),
    enumEntry("GL_LESS", GL_LESS),
    enumEntry("GL_EQUAL", GL_EQUAL),
    enumEntry("GL_LEQUAL", GL_LEQUAL),
    enumEntry("GL_GREATER", GL_GREATER),
    enumEntry("GL_NOTEQUAL", GL_NOTEQUAL),
    enumEntry("GL_GEQUAL", GL_GEQUAL),
    enumEntry("GL_ALWAYS", GL_ALWAYS),
    enumError,
    enumEnd
};
enumentry rndBlendFuncEnums[] =
{
    enumEntry("GL_ZERO", GL_ZERO),
    enumEntry("GL_ONE", GL_ONE),
    enumEntry("GL_DST_COLOR", GL_DST_COLOR),
    enumEntry("GL_ONE_MINUS_DST_COLOR", GL_ONE_MINUS_DST_COLOR),
    enumEntry("GL_SRC_ALPHA", GL_SRC_ALPHA),
    enumEntry("GL_ONE_MINUS_SRC_ALPHA", GL_ONE_MINUS_SRC_ALPHA),
    enumEntry("GL_DST_ALPHA", GL_DST_ALPHA),
    enumEntry("GL_ONE_MINUS_DST_ALPHA", GL_ONE_MINUS_DST_ALPHA),
    enumEntry("GL_SRC_ALPHA_SATURATE", GL_SRC_ALPHA_SATURATE),
    enumError,
    enumEnd
};
enumentry rndOrientationEnums[] =
{
    enumEntry("GL_CX", GL_CW),
    enumEntry("GL_CCW", GL_CCW),
    enumError,
    enumEnd
};
enumentry rndFaceEnums[] =
{
    enumEntry("GL_FRONT", GL_FRONT),
    enumEntry("GL_BACK", GL_BACK),
    enumEntry("GL_FRONT_AND_BACK", GL_FRONT_AND_BACK),
    enumError,
    enumEnd
};
enumentry rndMatrixEnums[] =
{
    enumEntry("GL_MODELVIEW", GL_MODELVIEW),
    enumEntry("GL_PROJECTION", GL_PROJECTION),
    enumEntry("GL_TEXTURE", GL_TEXTURE),
    enumError,
    enumEnd
};
enumentry rndHintEnums[] =
{
    enumEntry("GL_FASTEST", GL_FASTEST),
    enumEntry("GL_NICEST", GL_NICEST),
    enumEntry("GL_DONT_CARE", GL_DONT_CARE),
    enumError,
    enumEnd
};
enumentry rndShadeModelEnums[] =
{
    enumEntry("GL_FLAT", GL_FLAT),
    enumEntry("GL_SMOOTH", GL_SMOOTH),
    enumError,
    enumEnd
};
enumentry rndTextureEnvEnums[] =
{
    enumEntry("GL_MODULATE", GL_MODULATE),
    enumEntry("GL_DECAL", GL_DECAL),
    enumEntry("GL_BLEND", GL_BLEND),
    enumEntry("GL_REPLACE", GL_REPLACE),
    enumError,
    enumEnd
};

typedef struct
{
    const char *heading;
    GLenum enumeration;
    sdword type;
    sdword nValues;
    enumentry *enumTable;
}

glstateentry;

#define stateEntry(string, enumb, type, nValues, table)  {string, enumb, type, nValues, table}
#define G_Bool                  0
#define G_GetBool               1
#define G_Integer               2
#define G_Float                 3
#define G_FloatByte             4
#define G_IntFunc               5

//definitions for special-case functions
#define G_TextureEnv            0

glstateentry rndStateSaveTable[] =
{
    stateEntry("GL_ALPHA_TEST", GL_ALPHA_TEST,                                  G_Bool, 1, rndBoolEnums),
    stateEntry("GL_BLEND", GL_BLEND,                                            G_Bool, 1, rndBoolEnums),
    stateEntry("GL_CULL_FACE", GL_CULL_FACE,                                    G_Bool, 1, rndBoolEnums),
    stateEntry("GL_DEPTH_TEST", GL_DEPTH_TEST,                                  G_Bool, 1, rndBoolEnums),
    stateEntry("GL_FOG", GL_FOG,                                                G_Bool, 1, rndBoolEnums),
    stateEntry("GL_LIGHT0", GL_LIGHT0,                                          G_Bool, 1, rndBoolEnums),
    stateEntry("GL_LIGHT1", GL_LIGHT1,                                          G_Bool, 1, rndBoolEnums),
    stateEntry("GL_LIGHTING", GL_LIGHTING,                                      G_Bool, 1, rndBoolEnums),
    stateEntry("GL_LINE_SMOOTH", GL_LINE_SMOOTH,                                G_Bool, 1, rndBoolEnums),
    stateEntry("GL_LINE_STIPPLE", GL_LINE_STIPPLE,                              G_Bool, 1, rndBoolEnums),
    stateEntry("GL_NORMALIZE", GL_NORMALIZE,                                    G_Bool, 1, rndBoolEnums),
    stateEntry("GL_POINT_SMOOTH", GL_POINT_SMOOTH,                              G_Bool, 1, rndBoolEnums),
    stateEntry("GL_POLYGON_SMOOTH", GL_POLYGON_SMOOTH,                          G_Bool, 1, rndBoolEnums),
    stateEntry("GL_POLYGON_STIPPLE", GL_POLYGON_STIPPLE,                        G_Bool, 1, rndBoolEnums),
    stateEntry("GL_SCISSOR_TEST", GL_SCISSOR_TEST,                              G_Bool, 1, rndBoolEnums),
    stateEntry("GL_TEXTURE_2D", GL_TEXTURE_2D,                                  G_Bool, 1, rndBoolEnums),

    stateEntry("GL_RED_BITS", GL_RED_BITS,                                      G_Integer,   1, NULL),
    stateEntry("GL_GREEN_BITS", GL_GREEN_BITS,                                  G_Integer,   1, NULL),
    stateEntry("GL_BLUE_BITS", GL_BLUE_BITS,                                    G_Integer,   1, NULL),
    stateEntry("GL_ALPHA_BITS", GL_ALPHA_BITS,                                  G_Integer,   1, NULL),
    stateEntry("GL_DEPTH_BITS", GL_DEPTH_BITS,                                  G_Integer,   1, NULL),

    //stateEntry("GL_TEXTURE_ENV", G_TextureEnv,                                  G_IntFunc,   1, rndTextureEnvEnums),
    //stateEntry("GL_TEXTURE_2D_BINDING", GL_TEXTURE_2D_BINDING,                  G_Integer,   1, NULL),
    stateEntry("GL_ALPHA_TEST_FUNC", GL_ALPHA_TEST_FUNC,                        G_Integer,   1, rndAlphaTestFuncEnum),
    stateEntry("GL_ALPHA_TEST_REF", GL_ALPHA_TEST_REF,                          G_Float,     1, NULL),
    stateEntry("GL_ATTRIB_STACK_DEPTH", GL_ATTRIB_STACK_DEPTH,                  G_Integer,   1, NULL),
    stateEntry("GL_BLEND_DST", GL_BLEND_DST,                                    G_Integer,   1, rndBlendFuncEnums),
    stateEntry("GL_BLEND_SRC", GL_BLEND_SRC,                                    G_Integer,   1, rndBlendFuncEnums),
    stateEntry("GL_BLUE_BIAS", GL_BLUE_BIAS,                                    G_FloatByte, 1, NULL),
    stateEntry("GL_CLIENT_ATTRIB_STACK_DEPTH", GL_CLIENT_ATTRIB_STACK_DEPTH,    G_Integer,   1, NULL),
    stateEntry("GL_COLOR_CLEAR_VALUE", GL_COLOR_CLEAR_VALUE,                    G_Float,     4, NULL),
    stateEntry("GL_COLOR_MATERIAL_FACE", GL_COLOR_MATERIAL_FACE,                G_Integer,   1, rndFaceEnums),
    stateEntry("GL_CULL_FACE_MODE", GL_CULL_FACE_MODE,                          G_Integer,   1, rndFaceEnums),
    stateEntry("GL_CURRENT_COLOR", GL_CURRENT_COLOR,                            G_FloatByte, 4, NULL),
    stateEntry("GL_CURRENT_INDEX", GL_CURRENT_INDEX,                            G_Float,     1, NULL),
    stateEntry("GL_CURRENT_RASTER_COLOR", GL_CURRENT_RASTER_COLOR,              G_FloatByte, 4, NULL),
    stateEntry("GL_CURRENT_RASTER_POSITION", GL_CURRENT_RASTER_POSITION,        G_Float,     4, NULL),
    stateEntry("GL_CURRENT_TEXTURE_COORDS", GL_CURRENT_TEXTURE_COORDS,          G_Float,     4, NULL),
    stateEntry("GL_DEPTH_WRITEMASK", GL_DEPTH_WRITEMASK,                        G_GetBool,   1, rndBoolEnums),
    stateEntry("GL_FOG_COLOR", GL_FOG_COLOR,                                    G_FloatByte, 4, NULL),
    stateEntry("GL_FOG_DENSITY", GL_FOG_DENSITY,                                G_Float,     1, NULL),
    stateEntry("GL_FRONT_FACE", GL_FRONT_FACE,                                  G_Integer,   1, rndOrientationEnums),
    stateEntry("GL_GREEN_BIAS", GL_GREEN_BIAS,                                  G_FloatByte, 1, NULL),
    stateEntry("GL_LIGHT_MODEL_AMBIENT", GL_LIGHT_MODEL_AMBIENT,                G_FloatByte, 4, NULL),
    stateEntry("GL_LIGHT_MODEL_TWO_SIDE", GL_LIGHT_MODEL_TWO_SIDE,              G_GetBool,   1, rndBoolEnums),
    stateEntry("GL_LINE_WIDTH", GL_LINE_WIDTH,                                  G_Float,     1, NULL),
    stateEntry("GL_LINE_WIDTH_GRANULARITY", GL_LINE_WIDTH_GRANULARITY,          G_Float,     1, NULL),
    stateEntry("GL_MATRIX_MODE", GL_MATRIX_MODE,                                G_Integer,   1, rndMatrixEnums),
    stateEntry("GL_MAX_TEXTURE_SIZE", GL_MAX_TEXTURE_SIZE,                      G_Integer,   1, NULL),
    stateEntry("GL_MAX_VIEWPORT_DIMS", GL_MAX_VIEWPORT_DIMS,                    G_Integer,   2, NULL),
    stateEntry("GL_MODELVIEW_MATRIX", GL_MODELVIEW_MATRIX,                      G_Float,     16, NULL),
    stateEntry("GL_MODELVIEW_STACK_DEPTH", GL_MODELVIEW_STACK_DEPTH,            G_Integer,   1, NULL),
    stateEntry("GL_PERSPECTIVE_CORRECTION_HINT", GL_PERSPECTIVE_CORRECTION_HINT, G_Integer,   1, rndHintEnums),
    stateEntry("GL_POINT_SIZE", GL_POINT_SIZE,                                  G_Float,     1, NULL),
    stateEntry("GL_POINT_SIZE_GRANULARITY", GL_POINT_SIZE_GRANULARITY,          G_Float,     1, NULL),
    stateEntry("GL_POLYGON_MODE", GL_POLYGON_MODE,                              G_Integer,   1, rndFaceEnums),
    stateEntry("GL_PROJECTION_MATRIX", GL_PROJECTION_MATRIX,                    G_Float,     16, NULL),
    stateEntry("GL_PROJECTION_STACK_DEPTH", GL_PROJECTION_STACK_DEPTH,          G_Integer,   1, NULL),
    stateEntry("GL_RED_BIAS", GL_RED_BIAS,                                      G_FloatByte, 1, NULL),
    stateEntry("GL_SCISSOR_BOX", GL_SCISSOR_BOX,                                G_Integer,   4, NULL),
    stateEntry("GL_SHADE_MODEL", GL_SHADE_MODEL,                                G_Integer,   1, rndShadeModelEnums),
    stateEntry("GL_SUBPIXEL_BITS", GL_SUBPIXEL_BITS,                            G_Integer,   1, NULL),
    stateEntry("GL_VIEWPORT", GL_VIEWPORT,                                      G_Integer,   4, NULL),
    {NULL, 0, 0, 0, NULL}
};
char rndGLStateLogFileName[128];


/*=============================================================================
   Functions:
   =============================================================================*/

/*-----------------------------------------------------------------------------
   Name        : rndIntToString
   Description : Convert a GL enumeration to a string
   Inputs      : enumb - enumeration to convert
   table - table of enumerations/strings
   Outputs     :
   Return      : name of enumeration
   ----------------------------------------------------------------------------*/
static
const char *
rndIntToString(GLenum enumb,
               enumentry *table)
{
    sdword index;

    for (index = 0; table[index].name != NULL; index++) {
        if ( (enumb == table[index].enumeration) || table[index].bDefault ) {
            return(table[index].name);
        }
    }
    dbgAssert(FALSE);

    return NULL;
}

/*-----------------------------------------------------------------------------
   Name        : rndGLStateLog
   Description : Log the state of the GL machine.
   Inputs      : location - user-specified name for where this function is called from
   Outputs     :
   Return      : void
   ----------------------------------------------------------------------------*/
#define MAX_FLOATS          16
#define MAX_INTS            4
#define MAX_BOOLS           1
static
void
rndGLStateLogFunction(const char *location)
{
    sdword index, j;
    GLfloat floats[MAX_FLOATS];
    GLint ints[MAX_INTS];
    GLboolean bools[MAX_BOOLS];
    char totalString[256];
    char valueString[128];
    FILE *f;

    f = fopen(rndGLStateLogFileName, "at");
    if (f == NULL) {
        OFX::Log::error(true, "Error opening '%s' for GL state logging.", rndGLStateLogFileName);

        return;
    }

    fprintf(f, "******************** %s\n", location);
    for (index = 0; rndStateSaveTable[index].heading != NULL; index++) {
        sprintf(totalString, "%32s:", rndStateSaveTable[index].heading);
        switch (rndStateSaveTable[index].type) {
        case G_Bool:
            bools[0] = glIsEnabled(rndStateSaveTable[index].enumeration);
            for (j = 0; j < rndStateSaveTable[index].nValues; j++) {
                if (rndStateSaveTable[index].enumTable != NULL) {
                    sprintf( valueString, "%s", rndIntToString(bools[j], rndStateSaveTable[index].enumTable) );
                } else {
                    sprintf(valueString, "%d", bools[j]);
                }
                strcat(totalString, valueString);
                if (j + 1 < rndStateSaveTable[index].nValues) {
                    strcat(totalString, ", ");
                }
            }
            break;
        case G_GetBool:
            glGetBooleanv(rndStateSaveTable[index].enumeration, bools);
            for (j = 0; j < rndStateSaveTable[index].nValues; j++) {
                if (rndStateSaveTable[index].enumTable != NULL) {
                    sprintf( valueString, "%s", rndIntToString(bools[j], rndStateSaveTable[index].enumTable) );
                } else {
                    sprintf(valueString, "%d", bools[j]);
                }
                strcat(totalString, valueString);
                if (j + 1 < rndStateSaveTable[index].nValues) {
                    strcat(totalString, ", ");
                }
            }
            break;
        case G_Integer:
            glGetIntegerv(rndStateSaveTable[index].enumeration, ints);
            for (j = 0; j < rndStateSaveTable[index].nValues; j++) {
                if (rndStateSaveTable[index].enumTable != NULL) {
                    sprintf( valueString, "%s", rndIntToString(ints[j], rndStateSaveTable[index].enumTable) );
                } else {
                    sprintf(valueString, "%d", ints[j]);
                }
                strcat(totalString, valueString);
                if (j + 1 < rndStateSaveTable[index].nValues) {
                    strcat(totalString, ", ");
                }
            }
            break;
        case G_Float:
            glGetFloatv(rndStateSaveTable[index].enumeration, floats);
            for (j = 0; j < rndStateSaveTable[index].nValues; j++) {
                sprintf(valueString, "%.2g", floats[j]);
                strcat(totalString, valueString);
                if (j + 1 < rndStateSaveTable[index].nValues) {
                    strcat(totalString, ", ");
                }
            }
            break;
        case G_FloatByte:
            glGetFloatv(rndStateSaveTable[index].enumeration, floats);
            for (j = 0; j < rndStateSaveTable[index].nValues; j++) {
                sprintf( valueString, "%d", colRealToUbyte(floats[j]) );
                strcat(totalString, valueString);
                if (j + 1 < rndStateSaveTable[index].nValues) {
                    strcat(totalString, ", ");
                }
            }
            break;
        case G_IntFunc:
            switch (rndStateSaveTable[index].enumeration) {
            case G_TextureEnv:
                //                        glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, ints);
                break;
            default:
                dbgAssert(FALSE);
            }
            for (j = 0; j < rndStateSaveTable[index].nValues; j++) {
                if (rndStateSaveTable[index].enumTable != NULL) {
                    sprintf( valueString, "%s", rndIntToString(ints[j], rndStateSaveTable[index].enumTable) );
                } else {
                    sprintf(valueString, "%d", ints[j]);
                }
                strcat(totalString, valueString);
                if (j + 1 < rndStateSaveTable[index].nValues) {
                    strcat(totalString, ", ");
                }
            }
            break;
        default:
            dbgAssert(FALSE);
        } // switch
        fprintf(f, "%s\n", totalString);
    }
    fclose(f);
} // rndGLStateLogFunction

#endif //RND_GL_STATE_DEBUG
OFXS_NAMESPACE_ANONYMOUS_EXIT


void
TestOpenGLPlugin::RENDERFUNC(const OFX::RenderArguments &args)
{
    const double time = args.time;
    double scalex = 1;
    double scaley = 1;
    double sourceScalex = 1;
    double sourceScaley = 1;
    double sourceStretch = 0;
    double teapotScale = 1.;
    bool projective = true;
    bool mipmap = true;
    bool anisotropic = true;

    if (_scale) {
        _scale->getValueAtTime(time, scalex, scaley);
    }
    if (_sourceScale) {
        _sourceScale->getValueAtTime(time, sourceScalex, sourceScaley);
    }
    if (_sourceStretch) {
        _sourceStretch->getValueAtTime(time, sourceStretch);
    }
    if (_teapotScale) {
        _teapotScale->getValueAtTime(time, teapotScale);
    }
    if (_projective) {
        _projective->getValueAtTime(time, projective);
    }
    if (_mipmap) {
        _mipmap->getValueAtTime(time, mipmap);
    }
    if (_anisotropic) {
        _anisotropic->getValueAtTime(time, anisotropic);
    }

    if (args.renderQualityDraft) {
        mipmap = anisotropic = false;
    }

# ifdef OFX_SUPPORTS_OPENGLRENDER
    const int& gl_enabled = args.openGLEnabled;
    const OFX::ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    //DPRINT( ("render: openGLSuite %s\n", gHostDescription.supportsOpenGLRender ? "found" : "not found") );
    if (gHostDescription.supportsOpenGLRender) {
        DPRINT( ("render: openGL rendering %s\n", gl_enabled ? "enabled" : "DISABLED") );
    }
#  ifdef USE_OPENGL
    // For this test, we only process in OpenGL mode.
    if (!gl_enabled) {
        DPRINT( ("render: inside renderGL, but openGL rendering enabled\n") );

        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);

        return;
    }
#  endif
# endif

    const OfxRectI& renderWindow = args.renderWindow;
    DPRINT( ("renderWindow = [%d, %d - %d, %d]\n", renderWindow.x1, renderWindow.y1, renderWindow.x2, renderWindow.y2) );


    // get the output image texture
# ifdef USE_OPENGL
    std::auto_ptr<OFX::Texture> dst( _dstClip->loadTexture(time) );
# else
    std::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(time) );
# endif
    if ( !dst.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    OFX::BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);

        return;
    }
# ifdef USE_OPENGL
    const GLuint dstIndex = (GLuint)dst->getIndex();
    const GLenum dstTarget = (GLenum)dst->getTarget();
    DPRINT( ( "openGL: output texture index %d, target 0x%04X, depth %s\n",
              dstIndex, dstTarget, mapBitDepthEnumToStr(dstBitDepth) ) );
# endif
    OfxRectI dstBounds = dst->getBounds();
    DPRINT( ("dstBounds = [%d, %d - %d, %d]\n",
             dstBounds.x1, dstBounds.y1,
             dstBounds.x2, dstBounds.y2) );


# ifdef USE_OPENGL
    std::auto_ptr<const OFX::Texture> src( ( _srcClip && _srcClip->isConnected() ) ?
                                           _srcClip->loadTexture(time) : 0 );
# else
    std::auto_ptr<const OFX::Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                         _srcClip->fetchImage(time) : 0 );
# endif

    if ( !src.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    OFX::BitDepthEnum srcBitDepth      = src->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
    if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
        DPRINT( ( "render: (srcBitDepth=%s != dstBitDepth=%s) || (srcComponents=%s != dstComponents=%s)\n", mapBitDepthEnumToStr(srcBitDepth), mapBitDepthEnumToStr(dstBitDepth), mapPixelComponentEnumToStr(srcComponents), mapPixelComponentEnumToStr(dstComponents) ) );
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);

        return;
    }
# ifdef USE_OPENGL
    const GLuint srcIndex = (GLuint)src->getIndex();
    const GLenum srcTarget = (GLenum)src->getTarget();
    DPRINT( ( "openGL: source texture index %d, target 0x%04X, depth %s\n",
              srcIndex, srcTarget, mapBitDepthEnumToStr(srcBitDepth) ) );
# endif
    // XXX: check status for errors


#ifdef USE_OSMESA
    GLenum format;
    switch (srcComponents) {
    case OFX::ePixelComponentRGBA:
        format = GL_RGBA;
        break;
    case OFX::ePixelComponentAlpha:
        format = GL_ALPHA;
        break;
    default:
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);

        return;
    }
    GLint depthBits = 0;
    GLint stencilBits = 0;
    GLint accumBits = 0;
    GLenum type;
    switch (srcBitDepth) {
    case OFX::eBitDepthUByte:
        depthBits = 16;
        type = GL_UNSIGNED_BYTE;
        break;
    case OFX::eBitDepthUShort:
        depthBits = 16;
        type = GL_UNSIGNED_SHORT;
        break;
    case OFX::eBitDepthFloat:
        depthBits = 32;
        type = GL_FLOAT;
        break;
    default:
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);

        return;
    }
    /* Allocate the image buffer */
    void* buffer = dst->getPixelData();
    OSMesaPrivate *osmesa;
    {
        AutoMutex lock( _osmesaMutex.get() );
        if ( _osmesa.empty() ) {
            osmesa = new OSMesaPrivate(this);
        } else {
            osmesa = _osmesa.back();
            _osmesa.pop_back();
        }
    }
    if (OSMesaGetCurrentContext() != NULL) {
        DPRINT( ("render error: %s\n", "Mesa context still attached") );
        glFlush(); // waits until commands are submitted but does not wait for the commands to finish executing
        glFinish(); // waits for all previously submitted commands to complete executing
        // make sure the buffer is not referenced anymore
        OSMesaMakeCurrent(NULL, NULL, 0, 0, 0); // disactivate the context so that it can be used from another thread
    }
    assert(OSMesaGetCurrentContext() == NULL); // the thread should have no Mesa context attached
    osmesa->setContext(format, depthBits, type, stencilBits, accumBits, buffer, dstBounds);
#endif // ifdef USE_OSMESA

#ifdef USE_OPENGL
    OpenGLContextData* contextData = &_openGLContextData;
    if (OFX::getImageEffectHostDescription()->isNatron && !args.openGLContextData) {
        DPRINT( ("ERROR: Natron did not provide the contextData pointer to the OpenGL render func.\n") );
    }

    if (args.openGLContextData) {
        // host provided kNatronOfxImageEffectPropOpenGLContextData,
        // which was returned by kOfxActionOpenGLContextAttached
        contextData = (OpenGLContextData*)args.openGLContextData;
    } else if (!_openGLContextAttached) {
        // Sony Catalyst Edit never calls kOfxActionOpenGLContextAttached
        DPRINT( ("ERROR: OpenGL render() called without calling contextAttached() first. Calling it now.\n") );
        contextAttached(false);
        _openGLContextAttached = true;
    }
#endif
#ifdef USE_OSMESA
    OpenGLContextData* contextData = &osmesa->_openGLContextData;
#endif

#ifdef USE_OSMESA
    // load the source image into a texture
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLuint srcIndex;
    glGenTextures(1, &srcIndex);
    // Non-power-of-two textures are supported if the GL version is 2.0 or greater, or if the implementation exports the GL_ARB_texture_non_power_of_two extension. (Mesa does, of course)

    const GLenum srcTarget = GL_TEXTURE_2D;
    OfxRectI srcBounds = src->getBounds();
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glBindTexture(srcTarget, srcIndex);
    // legacy mipmap generation was replaced by glGenerateMipmap from GL_ARB_framebuffer_object (see below)
    if (mipmap && !glGenerateMipmap) {
        DPRINT( ("TestOpenGL: legacy mipmap generation!\n") );
        // this must be done before glTexImage2D
        glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
        // requires extension GL_SGIS_generate_mipmap or OpenGL 1.4.
        glTexParameteri(srcTarget, GL_GENERATE_MIPMAP, GL_TRUE); // Allocate the mipmaps
    }

    glTexImage2D( srcTarget, 0, format,
                  srcBounds.x2 - srcBounds.x1, srcBounds.y2 - srcBounds.y1, 0,
                  format, type, src->getPixelData() );


#endif // ifdef USE_OSMESA

#if RND_GL_STATE_DEBUG
#ifdef USE_OSMESA
    snprintf(rndGLStateLogFileName, sizeof(rndGLStateLogFileName), "ofxGLStateLogMesa.txt");
    rndGLStateLogFunction("TestOpenGLRender");
#else
    snprintf(rndGLStateLogFileName, sizeof(rndGLStateLogFileName), "ofxGLStateLogOpenGL.txt");
    rndGLStateLogFunction("TestOpenGLRender");
#endif
#endif

    const OfxPointD& rs = args.renderScale;
    DPRINT( ("renderScale = [%g, %g]\n",
             rs.x, rs.y) );

    bool haveAniso = contextData->haveAniso;
    float maxAnisoMax = contextData->maxAnisoMax;
    OfxRectD dstRoD = _dstClip->getRegionOfDefinition(time);
    double dstPAR = _dstClip->getPixelAspectRatio();
    //float w = (dstRoD.x2 - dstRoD.x1);
    //float h = (dstRoD.y2 - dstRoD.y1);

    // Render to texture: see http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-14-render-to-texture/

    // setup the projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    {
        // let us draw everything in canonical coordinates
        OfxRectD dstBoundsCanonical;
        OFX::Coords::toCanonical(dstBounds, rs, dstPAR, &dstBoundsCanonical);
        glOrtho( dstBoundsCanonical.x1, dstBoundsCanonical.x2,
                 dstBoundsCanonical.y1, dstBoundsCanonical.y2,
                 -10.0 * (dstRoD.y2 - dstRoD.y1), 10.0 * (dstRoD.y2 - dstRoD.y1) );
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_BLEND);

    // Draw black into dest to start
    glBegin(GL_QUADS);
    glColor4f(0, 0, 0, 1); //Set the colour to opaque black
    glVertex2f(dstRoD.x1, dstRoD.y1);
    glVertex2f(dstRoD.x1, dstRoD.y2);
    glVertex2f(dstRoD.x2, dstRoD.y2);
    glVertex2f(dstRoD.x2, dstRoD.y1);
    glEnd();

    //
    // Copy source texture to output by drawing a big textured quad
    //

    // set up texture (how much of this is needed?)
    glEnable(srcTarget);
    glBindTexture(srcTarget, srcIndex);
    glTexParameteri(srcTarget, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(srcTarget, GL_TEXTURE_WRAP_T, GL_REPEAT);
    //glTexParameteri(srcTarget, GL_TEXTURE_BASE_LEVEL, 0);
    //glTexParameteri(srcTarget, GL_TEXTURE_MAX_LEVEL, 1);
    //glTexParameterf(srcTarget, GL_TEXTURE_MIN_LOD, -1);
    //glTexParameterf(srcTarget, GL_TEXTURE_MAX_LOD, 1);
    glTexParameteri(srcTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#ifdef GL_ARB_framebuffer_object
    // https://www.opengl.org/wiki/Common_Mistakes#Automatic_mipmap_generation
    if (mipmap && glGenerateMipmap) {
        glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
        glGenerateMipmap(GL_TEXTURE_2D);  //Generate mipmaps now!!!
        glCheckError();
    }
#else
    if (mipmap) {
        // With opengl render, we don't know if mipmaps were generated by the host.
        // check if mipmaps exist for that texture (we only check if level 1 exists)

        int width = 0;
        glGetTexLevelParameteriv(srcTarget, 1, GL_TEXTURE_WIDTH, &width);
        if (width == 0) {
            mipmap = false;
        }
    }
#endif
    glTexParameteri(srcTarget, GL_TEXTURE_MIN_FILTER, mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    if (anisotropic && haveAniso) {
        glTexParameterf(srcTarget, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAnisoMax);
    }
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    // textures are oriented with Y up (standard orientation)
    //float tymin = 0;
    //float tymax = 1;

    {
        // we asked for the full src in getRegionsOfInterest()
        OfxRectD srcRoD = _srcClip->getRegionOfDefinition(time);
        double srcW = srcRoD.x2 - srcRoD.x1;
        double srcH = srcRoD.y2 - srcRoD.y1;

        // now draw the textured quad containing the source
        glBegin(GL_QUADS);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        if (projective) {
            glTexCoord4f (0, 0, 0, 1);
        } else {
            glTexCoord2f (0, 0);
        }
        glVertex2f   (0, 0);
        if (projective) {
            glTexCoord4f (1, 0, 0, 1);
        } else {
            glTexCoord2f (1, 0);
        }
        glVertex2f   (srcW * sourceScalex, 0);
        if (projective) {
            glTexCoord4f ( (1 - sourceStretch), (1 - sourceStretch), 0, (1 - sourceStretch) );
        } else {
            glTexCoord2f (1, 1);
        }
        glVertex2f   (srcW * sourceScalex * ( 1 + (1 - sourceStretch) ) / 2., srcH * sourceScaley);
        if (projective) {
            glTexCoord4f ( 0, (1 - sourceStretch), 0, (1 - sourceStretch) );
        } else {
            glTexCoord2f (0, 1);
        }
        glVertex2f   (srcW * sourceScalex * ( 1 - (1 - sourceStretch) ) / 2., srcH * sourceScaley);
        glEnd ();
    }
    glDisable(srcTarget);

    // Now draw some stuff on top of it to show we really did something
#define WIDTH 200
#define HEIGHT 100
    glBegin(GL_QUADS);
    glColor3f(1.0f, 0, 0); //Set the colour to red
    glVertex2f(10, 10);
    glVertex2f( 10, (10 + HEIGHT * scaley) );
    glVertex2f( (10 + WIDTH * scalex), (10 + HEIGHT * scaley) );
    glVertex2f( (10 + WIDTH * scalex), 10 );
    glEnd();

    // Now draw a teapot
    GLfloat light_ambient[] = {0.0, 0.0, 0.0, 1.0};
    GLfloat light_diffuse[] = {1.0, 1.0, 1.0, 1.0};
    GLfloat light_specular[] = {1.0, 1.0, 1.0, 1.0};
    /* light_position is NOT default value */
    GLfloat light_position[] = {1.0, 0.0, 0.0, 0.0};
    GLfloat global_ambient[] = {0.75, 0.75, 0.75, 1.0};

    glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, light_specular);
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);

    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, global_ambient);

    //glFrontFace(GL_CW); // the GLUT teapot is CW (default is CCW)
    //glCullFace(GL_BACK); // GL_BACK is default value
    //glEnable(GL_CULL_FACE); // disabled by default. the GLUT teapot does not work well with backface culling
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_AUTO_NORMAL);
    glEnable(GL_NORMALIZE);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    /*  material has small ambient reflection */
    GLfloat low_ambient[] = {0.1, 0.1, 0.1, 1.0};
    glMaterialfv(GL_FRONT, GL_AMBIENT, low_ambient);
    glMaterialf(GL_FRONT, GL_SHININESS, 40.0);
    glPushMatrix();
    glTranslatef( (dstRoD.x1 + dstRoD.x2) / 2, (dstRoD.y1 + dstRoD.y2) / 2, 0.0 );
    // get the angle parameters
    double angleX = 0;
    double angleY = 0;
    double angleZ = 0;
    if (_angleX) {
        _angleX->getValueAtTime(time, angleX);
    }
    if (_angleY) {
        _angleY->getValueAtTime(time, angleY);
    }
    if (_angleZ) {
        _angleZ->getValueAtTime(time, angleZ);
    }
    glRotatef(angleX, 1., 0., 0.);
    glRotatef(angleY, 0., 1., 0.);
    glRotatef(angleZ, 0., 0., 1.);
    glEnable(srcTarget); // it deserves testure
    glutSolidTeapot(teapotScale * (dstRoD.y2 - dstRoD.y1) / 4.);
    glDisable(srcTarget);
    glPopMatrix();

    // done; clean up.
    glPopAttrib();
    glCheckError();

#define DEBUG_OPENGL_BITS
#ifdef DEBUG_OPENGL_BITS
    {
        GLint r, g, b, a, d;
        glGetIntegerv(GL_RED_BITS, &r);
        glGetIntegerv(GL_GREEN_BITS, &g);
        glGetIntegerv(GL_BLUE_BITS, &b);
        glGetIntegerv(GL_ALPHA_BITS, &a);
        glGetIntegerv(GL_DEPTH_BITS, &d);
        DPRINT( ("channel sizes: %d %d %d %d\n", r, g, b, a) );
        DPRINT( ("depth bits %d\n", d) );
    }
#endif

#ifdef USE_OSMESA
    /* This is very important!!!
     * Make sure buffered commands are finished!!!
     */
    glDeleteTextures(1, &srcIndex);

    if ( !abort() ) {
        glFlush(); // waits until commands are submitted but does not wait for the commands to finish executing
        glFinish(); // waits for all previously submitted commands to complete executing
    }
    glCheckError();
    // make sure the buffer is not referenced anymore
    osmesa->setContext(format, depthBits, type, stencilBits, accumBits, NULL, dstBounds);
    OSMesaMakeCurrent(NULL, NULL, 0, 0, 0); // disactivate the context so that it can be used from another thread
    assert(OSMesaGetCurrentContext() == NULL);

    // We're finished with this osmesa, make it available for other renders
    {
        AutoMutex lock( _osmesaMutex.get() );
        _osmesa.push_back(osmesa);
    }
#endif
} // TestOpenGLPlugin::RENDERFUNC

static
void
getGlVersion(int *major,
             int *minor)
{
    const char *verstr = (const char *) glGetString(GL_VERSION);

    if ( (verstr == NULL) || (std::sscanf(verstr, "%d.%d", major, minor) != 2) ) {
        *major = *minor = 0;
        //fprintf(stderr, "Invalid GL_VERSION format!!!\n");
    }
}

#if 0
static
void
getGlslVersion(int *major,
               int *minor)
{
    int gl_major, gl_minor;

    getGlVersion(&gl_major, &gl_minor);

    *major = *minor = 0;
    if (gl_major == 1) {
        /* GL v1.x can only provide GLSL v1.00 as an extension */
        const char *extstr = (const char *) glGetString(GL_EXTENSIONS);
        if ( (extstr != NULL) &&
             (strstr(extstr, "GL_ARB_shading_language_100") != NULL) ) {
            *major = 1;
            *minor = 0;
        }
    } else if (gl_major >= 2) {
        /* GL v2.0 and greater must parse the version string */
        const char *verstr =
            (const char *) glGetString(GL_SHADING_LANGUAGE_VERSION);

        if ( (verstr == NULL) ||
             (std::sscanf(verstr, "%d.%d", major, minor) != 2) ) {
            *major = *minor = 0;
            //fprintf(stderr, "Invalid GL_SHADING_LANGUAGE_VERSION format!!!\n");
        }
    }
}

#endif

/*


   GL_RENDERER   = Gallium 0.4 on softpipe
   GL_VERSION    = 3.0 Mesa 11.2.2
   GL_VENDOR     = VMware, Inc.
   GL_EXTENSIONS = GL_ARB_multisample GL_EXT_abgr GL_EXT_bgra GL_EXT_blend_color GL_EXT_blend_minmax GL_EXT_blend_subtract GL_EXT_copy_texture GL_EXT_polygon_offset GL_EXT_subtexture GL_EXT_texture_object GL_EXT_vertex_array GL_EXT_compiled_vertex_array GL_EXT_texture GL_EXT_texture3D GL_IBM_rasterpos_clip GL_ARB_point_parameters GL_EXT_draw_range_elements GL_EXT_packed_pixels GL_EXT_point_parameters GL_EXT_rescale_normal GL_EXT_separate_specular_color GL_EXT_texture_edge_clamp GL_SGIS_generate_mipmap GL_SGIS_texture_border_clamp GL_SGIS_texture_edge_clamp GL_SGIS_texture_lod GL_ARB_framebuffer_sRGB GL_ARB_multitexture GL_EXT_framebuffer_sRGB GL_IBM_multimode_draw_arrays GL_IBM_texture_mirrored_repeat GL_ARB_texture_cube_map GL_ARB_texture_env_add GL_ARB_transpose_matrix GL_EXT_blend_func_separate GL_EXT_fog_coord GL_EXT_multi_draw_arrays GL_EXT_secondary_color GL_EXT_texture_env_add GL_EXT_texture_filter_anisotropic GL_EXT_texture_lod_bias GL_INGR_blend_func_separate GL_NV_blend_square GL_NV_light_max_exponent GL_NV_texgen_reflection GL_NV_texture_env_combine4 GL_SUN_multi_draw_arrays GL_ARB_texture_border_clamp GL_ARB_texture_compression GL_EXT_framebuffer_object GL_EXT_texture_env_combine GL_EXT_texture_env_dot3 GL_MESA_window_pos GL_NV_packed_depth_stencil GL_NV_texture_rectangle GL_ARB_depth_texture GL_ARB_occlusion_query GL_ARB_shadow GL_ARB_texture_env_combine GL_ARB_texture_env_crossbar GL_ARB_texture_env_dot3 GL_ARB_texture_mirrored_repeat GL_ARB_window_pos GL_EXT_stencil_two_side GL_EXT_texture_cube_map GL_NV_depth_clamp GL_NV_fog_distance GL_APPLE_packed_pixels GL_APPLE_vertex_array_object GL_ARB_draw_buffers GL_ARB_fragment_program GL_ARB_fragment_shader GL_ARB_shader_objects GL_ARB_vertex_program GL_ARB_vertex_shader GL_ATI_draw_buffers GL_ATI_texture_env_combine3 GL_ATI_texture_float GL_EXT_shadow_funcs GL_EXT_stencil_wrap GL_MESA_pack_invert GL_MESA_ycbcr_texture GL_NV_primitive_restart GL_ARB_depth_clamp GL_ARB_fragment_program_shadow GL_ARB_half_float_pixel GL_ARB_occlusion_query2 GL_ARB_point_sprite GL_ARB_shading_language_100 GL_ARB_sync GL_ARB_texture_non_power_of_two GL_ARB_vertex_buffer_object GL_ATI_blend_equation_separate GL_EXT_blend_equation_separate GL_OES_read_format GL_ARB_color_buffer_float GL_ARB_pixel_buffer_object GL_ARB_texture_compression_rgtc GL_ARB_texture_float GL_ARB_texture_rectangle GL_ATI_texture_compression_3dc GL_EXT_packed_float GL_EXT_pixel_buffer_object GL_EXT_texture_compression_rgtc GL_EXT_texture_mirror_clamp GL_EXT_texture_rectangle GL_EXT_texture_sRGB GL_EXT_texture_shared_exponent GL_ARB_framebuffer_object GL_EXT_framebuffer_blit GL_EXT_framebuffer_multisample GL_EXT_packed_depth_stencil GL_ARB_vertex_array_object GL_ATI_separate_stencil GL_ATI_texture_mirror_once GL_EXT_draw_buffers2 GL_EXT_draw_instanced GL_EXT_gpu_program_parameters GL_EXT_texture_array GL_EXT_texture_compression_latc GL_EXT_texture_integer GL_EXT_texture_sRGB_decode GL_EXT_timer_query GL_OES_EGL_image GL_ARB_copy_buffer GL_ARB_depth_buffer_float GL_ARB_draw_instanced GL_ARB_half_float_vertex GL_ARB_instanced_arrays GL_ARB_map_buffer_range GL_ARB_texture_rg GL_ARB_texture_swizzle GL_ARB_vertex_array_bgra GL_EXT_texture_swizzle GL_EXT_vertex_array_bgra GL_NV_conditional_render GL_AMD_conservative_depth GL_AMD_draw_buffers_blend GL_AMD_seamless_cubemap_per_texture GL_AMD_shader_stencil_export GL_ARB_ES2_compatibility GL_ARB_blend_func_extended GL_ARB_debug_output GL_ARB_draw_buffers_blend GL_ARB_draw_elements_base_vertex GL_ARB_explicit_attrib_location GL_ARB_fragment_coord_conventions GL_ARB_provoking_vertex GL_ARB_sampler_objects GL_ARB_seamless_cube_map GL_ARB_shader_stencil_export GL_ARB_shader_texture_lod GL_ARB_texture_cube_map_array GL_ARB_texture_gather GL_ARB_texture_multisample GL_ARB_texture_query_lod GL_ARB_texture_rgb10_a2ui GL_ARB_uniform_buffer_object GL_ARB_vertex_type_2_10_10_10_rev GL_EXT_provoking_vertex GL_EXT_texture_snorm GL_MESA_texture_signed_rgba GL_ARB_get_program_binary GL_ARB_robustness GL_ARB_separate_shader_objects GL_ARB_shader_bit_encoding GL_ARB_timer_query GL_ARB_transform_feedback2 GL_ARB_transform_feedback3 GL_ARB_base_instance GL_ARB_compressed_texture_pixel_storage GL_ARB_conservative_depth GL_ARB_internalformat_query GL_ARB_map_buffer_alignment GL_ARB_shading_language_420pack GL_ARB_shading_language_packing GL_ARB_texture_storage GL_ARB_transform_feedback_instanced GL_EXT_framebuffer_multisample_blit_scaled GL_EXT_transform_feedback GL_AMD_shader_trinary_minmax GL_ARB_ES3_compatibility GL_ARB_arrays_of_arrays GL_ARB_clear_buffer_object GL_ARB_explicit_uniform_location GL_ARB_invalidate_subdata GL_ARB_program_interface_query GL_ARB_stencil_texturing GL_ARB_texture_query_levels GL_ARB_texture_storage_multisample GL_ARB_texture_view GL_ARB_vertex_attrib_binding GL_KHR_debug GL_ARB_multi_bind GL_ARB_seamless_cubemap_per_texture GL_ARB_texture_mirror_clamp_to_edge GL_ARB_texture_stencil8 GL_ARB_vertex_type_10f_11f_11f_rev GL_EXT_shader_integer_mix GL_ARB_clip_control GL_ARB_conditional_render_inverted GL_ARB_get_texture_sub_image GL_ARB_pipeline_statistics_query GL_KHR_context_flush_control
   GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT = 16.000000

   GL_RENDERER   = Gallium 0.4 on llvmpipe (LLVM 3.8, 128 bits)
   GL_VERSION    = 3.0 Mesa 11.2.2
   GL_VENDOR     = VMware, Inc.
   GL_EXTENSIONS = GL_ARB_multisample GL_EXT_abgr GL_EXT_bgra GL_EXT_blend_color GL_EXT_blend_minmax GL_EXT_blend_subtract GL_EXT_copy_texture GL_EXT_polygon_offset GL_EXT_subtexture GL_EXT_texture_object GL_EXT_vertex_array GL_EXT_compiled_vertex_array GL_EXT_texture GL_EXT_texture3D GL_IBM_rasterpos_clip GL_ARB_point_parameters GL_EXT_draw_range_elements GL_EXT_packed_pixels GL_EXT_point_parameters GL_EXT_rescale_normal GL_EXT_separate_specular_color GL_EXT_texture_edge_clamp GL_SGIS_generate_mipmap GL_SGIS_texture_border_clamp GL_SGIS_texture_edge_clamp GL_SGIS_texture_lod GL_ARB_framebuffer_sRGB GL_ARB_multitexture GL_EXT_framebuffer_sRGB GL_IBM_multimode_draw_arrays GL_IBM_texture_mirrored_repeat GL_ARB_texture_cube_map GL_ARB_texture_env_add GL_ARB_transpose_matrix GL_EXT_blend_func_separate GL_EXT_fog_coord GL_EXT_multi_draw_arrays GL_EXT_secondary_color GL_EXT_texture_env_add GL_EXT_texture_lod_bias GL_INGR_blend_func_separate GL_NV_blend_square GL_NV_light_max_exponent GL_NV_texgen_reflection GL_NV_texture_env_combine4 GL_SUN_multi_draw_arrays GL_ARB_texture_border_clamp GL_ARB_texture_compression GL_EXT_framebuffer_object GL_EXT_texture_env_combine GL_EXT_texture_env_dot3 GL_MESA_window_pos GL_NV_packed_depth_stencil GL_NV_texture_rectangle GL_ARB_depth_texture GL_ARB_occlusion_query GL_ARB_shadow GL_ARB_texture_env_combine GL_ARB_texture_env_crossbar GL_ARB_texture_env_dot3 GL_ARB_texture_mirrored_repeat GL_ARB_window_pos GL_EXT_stencil_two_side GL_EXT_texture_cube_map GL_NV_depth_clamp GL_NV_fog_distance GL_APPLE_packed_pixels GL_APPLE_vertex_array_object GL_ARB_draw_buffers GL_ARB_fragment_program GL_ARB_fragment_shader GL_ARB_shader_objects GL_ARB_vertex_program GL_ARB_vertex_shader GL_ATI_draw_buffers GL_ATI_texture_env_combine3 GL_ATI_texture_float GL_EXT_shadow_funcs GL_EXT_stencil_wrap GL_MESA_pack_invert GL_MESA_ycbcr_texture GL_NV_primitive_restart GL_ARB_depth_clamp GL_ARB_fragment_program_shadow GL_ARB_half_float_pixel GL_ARB_occlusion_query2 GL_ARB_point_sprite GL_ARB_shading_language_100 GL_ARB_sync GL_ARB_texture_non_power_of_two GL_ARB_vertex_buffer_object GL_ATI_blend_equation_separate GL_EXT_blend_equation_separate GL_OES_read_format GL_ARB_color_buffer_float GL_ARB_pixel_buffer_object GL_ARB_texture_compression_rgtc GL_ARB_texture_float GL_ARB_texture_rectangle GL_ATI_texture_compression_3dc GL_EXT_packed_float GL_EXT_pixel_buffer_object GL_EXT_texture_compression_rgtc GL_EXT_texture_mirror_clamp GL_EXT_texture_rectangle GL_EXT_texture_sRGB GL_EXT_texture_shared_exponent GL_ARB_framebuffer_object GL_EXT_framebuffer_blit GL_EXT_framebuffer_multisample GL_EXT_packed_depth_stencil GL_ARB_vertex_array_object GL_ATI_separate_stencil GL_ATI_texture_mirror_once GL_EXT_draw_buffers2 GL_EXT_draw_instanced GL_EXT_gpu_program_parameters GL_EXT_texture_array GL_EXT_texture_compression_latc GL_EXT_texture_integer GL_EXT_texture_sRGB_decode GL_EXT_timer_query GL_OES_EGL_image GL_ARB_copy_buffer GL_ARB_depth_buffer_float GL_ARB_draw_instanced GL_ARB_half_float_vertex GL_ARB_instanced_arrays GL_ARB_map_buffer_range GL_ARB_texture_rg GL_ARB_texture_swizzle GL_ARB_vertex_array_bgra GL_EXT_texture_swizzle GL_EXT_vertex_array_bgra GL_NV_conditional_render GL_AMD_conservative_depth GL_AMD_draw_buffers_blend GL_AMD_seamless_cubemap_per_texture GL_AMD_shader_stencil_export GL_ARB_ES2_compatibility GL_ARB_blend_func_extended GL_ARB_debug_output GL_ARB_draw_buffers_blend GL_ARB_draw_elements_base_vertex GL_ARB_explicit_attrib_location GL_ARB_fragment_coord_conventions GL_ARB_provoking_vertex GL_ARB_sampler_objects GL_ARB_seamless_cube_map GL_ARB_shader_stencil_export GL_ARB_shader_texture_lod GL_ARB_texture_cube_map_array GL_ARB_texture_gather GL_ARB_texture_multisample GL_ARB_texture_rgb10_a2ui GL_ARB_uniform_buffer_object GL_ARB_vertex_type_2_10_10_10_rev GL_EXT_provoking_vertex GL_EXT_texture_snorm GL_MESA_texture_signed_rgba GL_ARB_get_program_binary GL_ARB_robustness GL_ARB_separate_shader_objects GL_ARB_shader_bit_encoding GL_ARB_timer_query GL_ARB_transform_feedback2 GL_ARB_transform_feedback3 GL_ARB_base_instance GL_ARB_compressed_texture_pixel_storage GL_ARB_conservative_depth GL_ARB_internalformat_query GL_ARB_map_buffer_alignment GL_ARB_shading_language_420pack GL_ARB_shading_language_packing GL_ARB_texture_storage GL_ARB_transform_feedback_instanced GL_EXT_framebuffer_multisample_blit_scaled GL_EXT_transform_feedback GL_AMD_shader_trinary_minmax GL_ARB_ES3_compatibility GL_ARB_arrays_of_arrays GL_ARB_clear_buffer_object GL_ARB_explicit_uniform_location GL_ARB_invalidate_subdata GL_ARB_program_interface_query GL_ARB_stencil_texturing GL_ARB_texture_query_levels GL_ARB_texture_storage_multisample GL_ARB_texture_view GL_ARB_vertex_attrib_binding GL_KHR_debug GL_ARB_buffer_storage GL_ARB_multi_bind GL_ARB_seamless_cubemap_per_texture GL_ARB_texture_mirror_clamp_to_edge GL_ARB_texture_stencil8 GL_ARB_vertex_type_10f_11f_11f_rev GL_EXT_shader_integer_mix GL_ARB_clip_control GL_ARB_conditional_render_inverted GL_ARB_get_texture_sub_image GL_EXT_polygon_offset_clamp GL_KHR_context_flush_control

   GL_RENDERER   = NVIDIA GeForce GT 330M OpenGL Engine
   GL_VERSION    = 2.1 NVIDIA-10.0.48 310.90.10.05b12
   GL_VENDOR     = NVIDIA Corporation
   GL_EXTENSIONS = GL_ARB_color_buffer_float GL_ARB_depth_buffer_float GL_ARB_depth_clamp GL_ARB_depth_texture GL_ARB_draw_buffers GL_ARB_draw_elements_base_vertex GL_ARB_draw_instanced GL_ARB_fragment_program GL_ARB_fragment_program_shadow GL_ARB_fragment_shader GL_ARB_framebuffer_object GL_ARB_framebuffer_sRGB GL_ARB_half_float_pixel GL_ARB_half_float_vertex GL_ARB_imaging GL_ARB_instanced_arrays GL_ARB_multisample GL_ARB_multitexture GL_ARB_occlusion_query GL_ARB_pixel_buffer_object GL_ARB_point_parameters GL_ARB_point_sprite GL_ARB_provoking_vertex GL_ARB_seamless_cube_map GL_ARB_shader_objects GL_ARB_shader_texture_lod GL_ARB_shading_language_100 GL_ARB_shadow GL_ARB_sync GL_ARB_texture_border_clamp GL_ARB_texture_compression GL_ARB_texture_compression_rgtc GL_ARB_texture_cube_map GL_ARB_texture_env_add GL_ARB_texture_env_combine GL_ARB_texture_env_crossbar GL_ARB_texture_env_dot3 GL_ARB_texture_float GL_ARB_texture_mirrored_repeat GL_ARB_texture_non_power_of_two GL_ARB_texture_rectangle GL_ARB_texture_rg GL_ARB_transpose_matrix GL_ARB_vertex_array_bgra GL_ARB_vertex_blend GL_ARB_vertex_buffer_object GL_ARB_vertex_program GL_ARB_vertex_shader GL_ARB_window_pos GL_EXT_abgr GL_EXT_bgra GL_EXT_bindable_uniform GL_EXT_blend_color GL_EXT_blend_equation_separate GL_EXT_blend_func_separate GL_EXT_blend_minmax GL_EXT_blend_subtract GL_EXT_clip_volume_hint GL_EXT_debug_label GL_EXT_debug_marker GL_EXT_depth_bounds_test GL_EXT_draw_buffers2 GL_EXT_draw_range_elements GL_EXT_fog_coord GL_EXT_framebuffer_blit GL_EXT_framebuffer_multisample GL_EXT_framebuffer_multisample_blit_scaled GL_EXT_framebuffer_object GL_EXT_framebuffer_sRGB GL_EXT_geometry_shader4 GL_EXT_gpu_program_parameters GL_EXT_gpu_shader4 GL_EXT_multi_draw_arrays GL_EXT_packed_depth_stencil GL_EXT_packed_float GL_EXT_provoking_vertex GL_EXT_rescale_normal GL_EXT_secondary_color GL_EXT_separate_specular_color GL_EXT_shadow_funcs GL_EXT_stencil_two_side GL_EXT_stencil_wrap GL_EXT_texture_array GL_EXT_texture_compression_dxt1 GL_EXT_texture_compression_s3tc GL_EXT_texture_env_add GL_EXT_texture_filter_anisotropic GL_EXT_texture_integer GL_EXT_texture_lod_bias GL_EXT_texture_mirror_clamp GL_EXT_texture_rectangle GL_EXT_texture_shared_exponent GL_EXT_texture_sRGB GL_EXT_texture_sRGB_decode GL_EXT_timer_query GL_EXT_transform_feedback GL_EXT_vertex_array_bgra GL_APPLE_aux_depth_stencil GL_APPLE_client_storage GL_APPLE_element_array GL_APPLE_fence GL_APPLE_float_pixels GL_APPLE_flush_buffer_range GL_APPLE_flush_render GL_APPLE_object_purgeable GL_APPLE_packed_pixels GL_APPLE_pixel_buffer GL_APPLE_rgb_422 GL_APPLE_row_bytes GL_APPLE_specular_vector GL_APPLE_texture_range GL_APPLE_transform_hint GL_APPLE_vertex_array_object GL_APPLE_vertex_array_range GL_APPLE_vertex_point_size GL_APPLE_vertex_program_evaluators GL_APPLE_ycbcr_422 GL_ATI_separate_stencil GL_ATI_texture_env_combine3 GL_ATI_texture_float GL_ATI_texture_mirror_once GL_IBM_rasterpos_clip GL_NV_blend_square GL_NV_conditional_render GL_NV_depth_clamp GL_NV_fog_distance GL_NV_fragment_program_option GL_NV_fragment_program2 GL_NV_light_max_exponent GL_NV_multisample_filter_hint GL_NV_point_sprite GL_NV_texgen_reflection GL_NV_texture_barrier GL_NV_vertex_program2_option GL_NV_vertex_program3 GL_SGIS_generate_mipmap GL_SGIS_texture_edge_clamp GL_SGIS_texture_lod
   GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT = 16.000000
 */

#ifdef DEBUG
static inline
bool
isspace(char c)
{
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

#endif

/*
 * Action called when an effect has just been attached to an OpenGL
 * context.
 *
 * The purpose of this action is to allow a plugin to set up any data it may need
 * to do OpenGL rendering in an instance. For example...
 *  - allocate a lookup table on a GPU,
 *  - create an openCL or CUDA context that is bound to the host's OpenGL
 *    context so it can share buffers.
 *
 * Note: Sony Catalyst Edit never calls contextAttached or contextDetached, so the
 * render action should call contextAttached on first openGL render if it was not
 * called by the host.
 */
void*
TestOpenGLPlugin::contextAttached(bool createContextData)
{
    DPRINT( ( "GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER) ) );
    DPRINT( ( "GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION) ) );
    DPRINT( ( "GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR) ) );
#ifdef DEBUG
    DPRINT( ( "GL_EXTENSIONS =" ) );
    const char *s = (const char*)glGetString(GL_EXTENSIONS);
    unsigned p = 0, end = 0;
    char elem[1024];
    while (s[p]) {
        while ( s[p] && isspace(s[p]) ) {
            ++p;
        }
        end = p;
        while ( s[end] && !isspace(s[end]) ) {
            ++end;
        }
        if (end != p) {
            assert( (end - p) < (sizeof(elem) - 1) );
            std::copy(s + p, s + end, elem);
            elem[end - p] = 0;
            DPRINT( ( " %s", elem ) );
        }
        p = end;
    }
    DPRINT( ( "\n" ) );
#endif

    {
        AutoMutex lock( _rendererInfoMutex.get() );
#ifdef USE_OSMESA
        std::string &message = _rendererInfoMesa;
#else
        std::string &message = _rendererInfoGL;
#endif
        if ( message.empty() ) {
#ifdef USE_OSMESA
            message += "OpenGL CPU renderer information:";
#else
            message += "OpenGL GPU renderer information:";
#endif
            message += "\nGL_RENDERER = ";
            message += (char *) glGetString(GL_RENDERER);
            message += "\nGL_VERSION = ";
            message += (char *) glGetString(GL_VERSION);
            message += "\nGL_VENDOR = ";
            message += (char *) glGetString(GL_VENDOR);
            message += "\nGL_SHADING_LANGUAGE_VERSION = ";
            message += (char *) glGetString(GL_SHADING_LANGUAGE_VERSION);
            message += "\nGL_EXTENSIONS = ";
            message += (char *) glGetString(GL_EXTENSIONS);
        }
    }

    // Non-power-of-two textures are supported if the GL version is 2.0 or greater, or if the implementation exports the GL_ARB_texture_non_power_of_two extension. (Mesa does, of course)
    int major, minor;
    getGlVersion(&major, &minor);
    if (major < 2) {
        if ( !glutExtensionSupported("GL_ARB_texture_non_power_of_two") ) {
            sendMessage(OFX::Message::eMessageError, "", "Can not render: OpenGL 2.0 or GL_ARB_texture_non_power_of_two is required.");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    //if (major < 3) {
    //    if ( (major == 2) && (minor < 1) ) {
    //        sendMessage(OFX::Message::eMessageError, "", "Can not render: OpenGL 2.1 or better required for GLSL support.");
    //        OFX::throwSuiteStatusException(kOfxStatFailed);
    //    }
    //}

    OpenGLContextData* contextData = &_openGLContextData;
#ifdef USE_OPENGL
#ifdef DEBUG
    if (OFX::getImageEffectHostDescription()->isNatron && !createContextData) {
        DPRINT( ("Error: Natron did not ask to create context data\n") );
    }
#endif
    if (createContextData) {
        contextData = new OpenGLContextData;
    }
#else
    assert(!createContextData); // context data is handled differently in CPU rendering
#endif

    contextData->haveAniso = glutExtensionSupported("GL_EXT_texture_filter_anisotropic");
    if (contextData->haveAniso) {
        GLfloat MaxAnisoMax;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &MaxAnisoMax);
        contextData->maxAnisoMax = MaxAnisoMax;
        DPRINT( ("GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT = %f\n", contextData->maxAnisoMax) );
    }

#if !defined(USE_OSMESA) && ( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) )
    if (glGenerateMipmap == NULL) {
        // Program
#ifdef GL_VERSION_2_0
        // glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
        // glDeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");
        // glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
        // glAttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
        // glDetachShader = (PFNGLDETACHSHADERPROC)wglGetProcAddress("glDetachShader");
        // glLinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
        // glGetProgramiv = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
        // glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)wglGetProcAddress("glGetProgramInfoLog");
        // glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
        // glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
        // glUniform1i = (PFNGLUNIFORM1IPROC)wglGetProcAddress("glUniform1i");
        // glUniform2i = (PFNGLUNIFORM2IPROC)wglGetProcAddress("glUniform2i");
        // glUniform3i = (PFNGLUNIFORM3IPROC)wglGetProcAddress("glUniform3i");
        // glUniform4i = (PFNGLUNIFORM4IPROC)wglGetProcAddress("glUniform4i");
        // glUniform1iv = (PFNGLUNIFORM1IVPROC)wglGetProcAddress("glUniform1iv");
        // glUniform2iv = (PFNGLUNIFORM2IVPROC)wglGetProcAddress("glUniform2iv");
        // glUniform3iv = (PFNGLUNIFORM3IVPROC)wglGetProcAddress("glUniform3iv");
        // glUniform4iv = (PFNGLUNIFORM4IVPROC)wglGetProcAddress("glUniform4iv");
        // glUniform1f = (PFNGLUNIFORM1FPROC)wglGetProcAddress("glUniform1f");
        // glUniform2f = (PFNGLUNIFORM2FPROC)wglGetProcAddress("glUniform2f");
        // glUniform3f = (PFNGLUNIFORM3FPROC)wglGetProcAddress("glUniform3f");
        // glUniform4f = (PFNGLUNIFORM4FPROC)wglGetProcAddress("glUniform4f");
        // glUniform1fv = (PFNGLUNIFORM1FVPROC)wglGetProcAddress("glUniform1fv");
        // glUniform2fv = (PFNGLUNIFORM2FVPROC)wglGetProcAddress("glUniform2fv");
        // glUniform3fv = (PFNGLUNIFORM3FVPROC)wglGetProcAddress("glUniform3fv");
        // glUniform4fv = (PFNGLUNIFORM4FVPROC)wglGetProcAddress("glUniform4fv");
        // glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)wglGetProcAddress("glUniformMatrix4fv");
        // glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)wglGetProcAddress("glGetAttribLocation");
        // glVertexAttrib1f = (PFNGLVERTEXATTRIB1FPROC)wglGetProcAddress("glVertexAttrib1f");
        // glVertexAttrib1fv = (PFNGLVERTEXATTRIB1FVPROC)wglGetProcAddress("glVertexAttrib1fv");
        // glVertexAttrib2fv = (PFNGLVERTEXATTRIB2FVPROC)wglGetProcAddress("glVertexAttrib2fv");
        // glVertexAttrib3fv = (PFNGLVERTEXATTRIB3FVPROC)wglGetProcAddress("glVertexAttrib3fv");
        // glVertexAttrib4fv = (PFNGLVERTEXATTRIB4FVPROC)wglGetProcAddress("glVertexAttrib4fv");
        // glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
        // glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
        // glBindAttribLocation = (PFNGLBINDATTRIBLOCATIONPROC)wglGetProcAddress("glBindAttribLocation");
#endif

        // Shader
#ifdef GL_VERSION_2_0
        // glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
        // glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
        // glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
        // glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
        // glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
#endif

        // VBO
#ifdef GL_VERSION_1_5
        // glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
        // glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
        // glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
#endif

        // Multitexture
#ifdef GL_VERSION_1_3
        // glActiveTexture = (PFNGLACTIVETEXTUREARBPROC)wglGetProcAddress("glActiveTexture");
#endif
#ifdef GL_VERSION_1_3_DEPRECATED
        //glClientActiveTexture = (PFNGLCLIENTACTIVETEXTUREPROC)wglGetProcAddress("glClientActiveTexture");
        //glMultiTexCoord2f = (PFNGLMULTITEXCOORD2FPROC)wglGetProcAddress("glMultiTexCoord2f");
#endif

        // Framebuffers
#ifdef GL_ARB_framebuffer_object
        //glIsFramebuffer = (PFNGLISFRAMEBUFFERPROC)wglGetProcAddress("glIsFramebuffer");
        // glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
        // glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)wglGetProcAddress("glDeleteFramebuffers");
        // glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
        // glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");
        //glFramebufferTexture1D = (PFNGLFRAMEBUFFERTEXTURE1DPROC)wglGetProcAddress("glFramebufferTexture1D");
        // glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
        //glFramebufferTexture3D = (PFNGLFRAMEBUFFERTEXTURE3DPROC)wglGetProcAddress("glFramebufferTexture3D");
        //glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)wglGetProcAddress("glFramebufferRenderbuffer");
        //glGetFramebufferAttachmentParameteriv = (PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC)wglGetProcAddress("glGetFramebufferAttachmentParameteriv");
        glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)wglGetProcAddress("glGenerateMipmap");
#endif

#ifdef GL_ARB_sync
        // Sync Objects https://www.opengl.org/wiki/Sync_Object
        // glFenceSync = (PFNGLFENCESYNCPROC)wglGetProcAddress("glFenceSync​");
        // glIsSync = (PFNGLISSYNCPROC)wglGetProcAddress("glIsSync");
        // glDeleteSync = (PFNGLDELETESYNCPROC)wglGetProcAddress("glDeleteSync");
        // glClientWaitSync = (PFNGLCLIENTWAITSYNCPROC)wglGetProcAddress("glClientWaitSync​");
        // glWaitSync = (PFNGLWAITSYNCPROC)wglGetProcAddress("glWaitSync​");
#endif
    }
#endif // if !defined(USE_OSMESA) && ( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) )

#ifdef USE_OPENGL
    if (createContextData) {
        return contextData;
    }
#else
#endif

    return NULL;
} // TestOpenGLPlugin::contextAttached

/*
 * Action called when an effect is about to be detached from an
 * OpenGL context
 *
 * The purpose of this action is to allow a plugin to deallocate any resource
 * allocated in \ref ::kOfxActionOpenGLContextAttached just before the host
 * decouples a plugin from an OpenGL context.
 * The host must call this with the same OpenGL context active as it
 * called with the corresponding ::kOfxActionOpenGLContextAttached.
 */
void
TestOpenGLPlugin::contextDetached(void* contextData)
{
#ifdef USE_OPENGL
    if (contextData) {
        delete (OpenGLContextData*)contextData;
    } else {
        _openGLContextAttached = false;
    }
#else
    assert(!contextData); // context data is handled differently in CPU rendering
#endif
}

