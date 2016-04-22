/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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

// first, check that the file is used in a good way
#if !defined(USE_OPENGL) && !defined(USE_OSMESA)
#error "USE_OPENGL or USE_OSMESA must be defined before including this file."
#endif
#if defined(USE_OPENGL) && defined(USE_OSMESA)
#error "include this file first only once, either with USE_OPENGL, or with USE_OSMESA"
#endif

#if defined(USE_OSMESA) || !( defined(_WIN32) || defined(__WIN32__) || defined(WIN32) )
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
#  include <OpenGL/glu.h>
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
#  include <GL/glu.h>
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

// put a breakpoint in glError to halt the debugger
inline void glError() {}

#define glCheckError()                                                  \
    {                                                                   \
        GLenum _glerror_ = glGetError();                                \
        if (_glerror_ != GL_NO_ERROR) {                                 \
            std::cout << "GL_ERROR :" << __FILE__ << " " << __LINE__ << " " << gluErrorString(_glerror_) << std::endl; \
            glError();                                                  \
        }                                                               \
    }

#define DPRINT(args) print_dbg args
static
void print_dbg(const char *format, ...)
{
    char str[1024];
    va_list ap;

    va_start(ap, format);
    size_t size = sizeof(str);
#if defined(_MSC_VER)
#  if _MSC_VER >= 1400
    vsnprintf_s(str, size, _TRUNCATE, format, ap);
#  else
    if (size == 0)        /* not even room for a \0? */
        return -1;        /* not what C99 says to do, but what windows does */
    str[size-1] = '\0';
    _vsnprintf(str, size-1, format, ap);
#  endif
#else
    vsnprintf(str, size, format, ap);
#endif
    std::fwrite(str, sizeof(char), std::strlen(str), stderr);
    std::fflush(stderr);
#ifdef _WIN32
    OutputDebugString(msg);
#endif
    va_end(ap);
}
#endif

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

    ~OSMesaPrivate() {
        /* destroy the context */
        if (_ctx) {
            // make the context current, with a dummy buffer
            unsigned char buffer[4];
            OSMesaMakeCurrent(_ctx, buffer, GL_UNSIGNED_BYTE, 1, 1);
            _effect->contextDetachedMesa();
            OSMesaMakeCurrent(_ctx, NULL, 0, 0, 0); // detach buffer from context
            OSMesaMakeCurrent(NULL, NULL, 0, 0, 0); // disactivate the context (not really recessary)
            OSMesaDestroyContext( _ctx );
            assert(!OSMesaGetCurrentContext());
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
        if (!_ctx || (format      != _ctxFormat &&
                      depthBits   != _ctxDepthBits &&
                      stencilBits != _ctxStencilBits &&
                      accumBits   != _ctxAccumBits)) {
            /* destroy the context */
            if (_ctx) {
                //printf("%p before OSMesaDestroyContext(%p), OSMesaGetCurrentContext=%p\n", pthread_self(), _ctx, OSMesaGetCurrentContext());
                // make the context current, with a dummy buffer
                unsigned char buffer[4];
                OSMesaMakeCurrent(_ctx, buffer, GL_UNSIGNED_BYTE, 1, 1);
                _effect->contextDetachedMesa();
                OSMesaMakeCurrent(_ctx, NULL, 0, 0, 0); // detach buffer from context
                OSMesaMakeCurrent(NULL, NULL, 0, 0, 0); // disactivate the context (not really recessary)
                OSMesaDestroyContext( _ctx );
                assert(!OSMesaGetCurrentContext());
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
                DPRINT(("OSMesaCreateContext failed!\n"));
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
        if (!OSMesaMakeCurrent( _ctx, buffer, type, dstBounds.x2 - dstBounds.x1, dstBounds.y2 - dstBounds.y1 )) {
            DPRINT(("OSMesaMakeCurrent failed!\n"));
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
        //OSMesaPixelStore(OSMESA_Y_UP, true); // default value
        //OSMesaPixelStore(OSMESA_ROW_LENGTH, dstBounds.x2 - dstBounds.x1); // default value
        if (newContext) {
            _effect->contextAttachedMesa();
        } else {
            // set viewport
            glViewport(0, 0, dstBounds.x2 - dstBounds.x1, dstBounds.y2 - dstBounds.y1);
        }
    }

    OSMesaContext ctx() { return _ctx; }

    TestOpenGLPlugin *_effect;
    // information about the current Mesa context
    OSMesaContext _ctx;
    GLenum _ctxFormat;
    GLint _ctxDepthBits;
    GLint _ctxStencilBits;
    GLint _ctxAccumBits;
};


void
TestOpenGLPlugin::initMesa()
{
}


void
TestOpenGLPlugin::exitMesa()
{
    OFX::MultiThread::AutoMutex lock(_osmesaMutex);
    for (std::list<OSMesaPrivate *>::iterator it = _osmesa.begin(); it != _osmesa.end(); ++it) {
        delete *it;
    }
    _osmesa.clear();
}
#endif

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
    101, 0, 1, 2, 3,},
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
teapot(GLint grid, GLdouble scale, GLenum type)
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
                    if (l == 1)
                        q[j][k][l] *= -1.0;
                    if (i < 6) {
                        r[j][k][l] =
                        cpdata[patchdata[i][j * 4 + (3 - k)]][l];
                        if (l == 0)
                            r[j][k][l] *= -1.0;
                        s[j][k][l] = cpdata[patchdata[i][j * 4 + k]][l];
                        if (l == 0)
                            s[j][k][l] *= -1.0;
                        if (l == 1)
                            s[j][k][l] *= -1.0;
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
}

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
int glutExtensionSupported( const char* extension )
{
    const char *extensions, *start;
    const size_t len = std::strlen( extension );

    /* Make sure there is a current window, and thus a current context available */
    //FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutExtensionSupported" );
    //freeglut_return_val_if_fail( fgStructure.CurrentWindow != NULL, 0 );

    if (std::strchr(extension, ' '))
        return 0;
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
        if (!p)
            return 0;  /* not found */
        /* check that the match isn't a super string */
        if ((p == start || p[-1] == ' ') && (p[len] == ' ' || p[len] == 0))
            return 1;
        /* skip the false match and continue */
        extensions = p + len;
    }
    
    return 0 ;
}

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
    DPRINT(("render: openGLSuite %s\n", gHostDescription.supportsOpenGLRender ? "found" : "not found"));
    if (gHostDescription.supportsOpenGLRender) {
        DPRINT(("render: openGL rendering %s\n", gl_enabled ? "enabled" : "DISABLED"));
    }
#  ifdef USE_OPENGL
    // For this test, we only process in OpenGL mode.
    if (!gl_enabled) {
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        return;
    }
#  endif
# endif

    const OfxRectI renderWindow = args.renderWindow;
    DPRINT(("renderWindow = [%d, %d - %d, %d]\n",
            renderWindow.x1, renderWindow.y1,
            renderWindow.x2, renderWindow.y2));


    // get the output image texture
# ifdef USE_OPENGL
    std::auto_ptr<OFX::Texture> dst(_dstClip->loadTexture(time));
# else
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(time));
# endif
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
# ifdef USE_OPENGL
    const GLuint dstIndex = (GLuint)dst->getIndex();
    const GLenum dstTarget = (GLenum)dst->getTarget();
    DPRINT(("openGL: output texture index %d, target %d, depth %s\n",
            dstIndex, dstTarget, mapBitDepthEnumToStr(dstBitDepth)));
# endif

# ifdef USE_OPENGL
    std::auto_ptr<const OFX::Texture> src((_srcClip && _srcClip->isConnected()) ?
                                          _srcClip->loadTexture(time) : 0);
# else
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                          _srcClip->fetchImage(time) : 0);
# endif

    if (!src.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
    if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        return;
    }
# ifdef USE_OPENGL
    const GLuint srcIndex = (GLuint)src->getIndex();
    const GLenum srcTarget = (GLenum)src->getTarget();
    DPRINT(("openGL: source texture index %d, target %d, depth %s\n",
            srcIndex, srcTarget, mapBitDepthEnumToStr(srcBitDepth)));
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
    OfxRectI dstBounds = dst->getBounds();
    OSMesaPrivate *osmesa;
    {
        OFX::MultiThread::AutoMutex lock(_osmesaMutex);
        if (_osmesa.empty()) {
            osmesa = new OSMesaPrivate(this);
        } else {
            osmesa = _osmesa.back();
            _osmesa.pop_back();
        }
    }
    assert(OSMesaGetCurrentContext() == NULL); // the thread should have no Mesa context attached
    osmesa->setContext(format, depthBits, type, stencilBits, accumBits, buffer, dstBounds);

    // load the source image into a texture
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLuint srcIndex;
    glGenTextures(1, &srcIndex);
    // Non-power-of-two textures are supported if the GL version is 2.0 or greater, or if the implementation exports the GL_ARB_texture_non_power_of_two extension. (Mesa does, of course)

    const GLenum srcTarget = GL_TEXTURE_2D;
    OfxRectI srcBounds = src->getBounds();
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glBindTexture(srcTarget, srcIndex);
    if (mipmap) {
        // this must be done before glTexImage2D
        glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
        // requires extension GL_SGIS_generate_mipmap or OpenGL 1.4.
        glTexParameteri(srcTarget, GL_GENERATE_MIPMAP, GL_TRUE); // Allocate the mipmaps
    }

    glTexImage2D(srcTarget, 0, format,
                 srcBounds.x2 - srcBounds.x1, srcBounds.y2 - srcBounds.y1, 0,
                 format, type, src->getPixelData());

    // setup the projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(dstBounds.x1, dstBounds.x2,
            dstBounds.y1, dstBounds.y2,
            -10.0*(dstBounds.y2-dstBounds.y1), 10.0*(dstBounds.y2-dstBounds.y1));
    glMatrixMode(GL_MODELVIEW);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    DPRINT(("dstBounds = [%d, %d - %d, %d]\n",
            dstBounds.x1, dstBounds.y1,
            dstBounds.x2, dstBounds.y2));

#endif

    const OfxPointD& rs = args.renderScale;
    DPRINT(("renderScale = [%g, %d]\n",
            rs.x, rs.y));

    // Render to texture: see http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-14-render-to-texture/
    float w = (renderWindow.x2 - renderWindow.x1);
    float h = (renderWindow.y2 - renderWindow.y1);

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_BLEND);

    // Draw black into dest to start
    glBegin(GL_QUADS);
    glColor4f(0, 0, 0, 1); //Set the colour to opaque black
    glVertex2f(0, 0);
    glVertex2f(0, h);
    glVertex2f(w, h);
    glVertex2f(w, 0);
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
    // With opengl render, we don't know if mipmaps were generated by the host.
    // check if mipmaps exist for that texture (we only check if level 1 exists)
    {
        int width = 0;
        glGetTexLevelParameteriv(srcTarget, 1, GL_TEXTURE_WIDTH, &width);
        if (width == 0) {
            mipmap = false;
        }
    }
    glTexParameteri(srcTarget, GL_TEXTURE_MIN_FILTER, mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    if (anisotropic && _haveAniso) {
        glTexParameterf(srcTarget, GL_TEXTURE_MAX_ANISOTROPY_EXT, _maxAnisoMax);
    }
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    // textures are oriented with Y up (standard orientation)
    //float tymin = 0;
    //float tymax = 1;

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
    glVertex2f   (w * sourceScalex, 0);
    if (projective) {
        glTexCoord4f ((1 - sourceStretch), (1 - sourceStretch), 0, (1 - sourceStretch));
    } else {
        glTexCoord2f (1, 1);
    }
    glVertex2f   (w * sourceScalex * (1 + (1 - sourceStretch)) / 2., h * sourceScaley);
    if (projective) {
        glTexCoord4f (0, (1 - sourceStretch), 0, (1 - sourceStretch));
    } else {
        glTexCoord2f (0, 1);
    }
    glVertex2f   (w * sourceScalex * (1 - (1 - sourceStretch)) / 2., h * sourceScaley);
    glEnd ();

    glDisable(srcTarget);

    // Now draw some stuff on top of it to show we really did something
#define WIDTH 200
#define HEIGHT 100
    glBegin(GL_QUADS);
    glColor3f(1.0f, 0, 0); //Set the colour to red
    glVertex2f(10 * rs.x, 10 * rs.y);
    glVertex2f(10 * rs.x, (10 + HEIGHT * scaley) * rs.y);
    glVertex2f((10 + WIDTH * scalex) * rs.x, (10 + HEIGHT * scaley) * rs.y);
    glVertex2f((10 + WIDTH * scalex) * rs.x, 10 * rs.y);
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

    glFrontFace(GL_CW);
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
    glTranslatef(w/2., h/2., 0.0);
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
    glutSolidTeapot(teapotScale * h/4.);
    glDisable(srcTarget);
    glPopMatrix();

    // done; clean up.
    glPopAttrib();

#ifdef USE_OSMESA
    /* This is very important!!!
     * Make sure buffered commands are finished!!!
     */
    glDeleteTextures(1, &srcIndex);

#ifdef DEBUG
    {
        GLint r, g, b, a, d;
        glGetIntegerv(GL_RED_BITS, &r);
        glGetIntegerv(GL_GREEN_BITS, &g);
        glGetIntegerv(GL_BLUE_BITS, &b);
        glGetIntegerv(GL_ALPHA_BITS, &a);
        glGetIntegerv(GL_DEPTH_BITS, &d);
        DPRINT(("channel sizes: %d %d %d %d\n", r, g, b, a));
        DPRINT(("depth bits %d\n", d));
    }
#endif
    glFinish();
    // make sure the buffer is not referenced anymore
    osmesa->setContext(format, depthBits, type, stencilBits, accumBits, NULL, dstBounds);
    OSMesaMakeCurrent(NULL, NULL, 0, 0, 0); // disactivate the context so that it can be used from another thread
    assert(OSMesaGetCurrentContext() == NULL);

    // We're finished with this osmesa, make it available for other renders
    {
        OFX::MultiThread::AutoMutex lock(_osmesaMutex);
        _osmesa.push_back(osmesa);
    }
#endif
}

static
void getGlVersion(int *major, int *minor)
{
    const char *verstr = (const char *) glGetString(GL_VERSION);
    if ((verstr == NULL) || (std::sscanf(verstr,"%d.%d", major, minor) != 2)) {
        *major = *minor = 0;
        //fprintf(stderr, "Invalid GL_VERSION format!!!\n");
    }
}

#if 0
static
void getGlslVersion(int *major, int *minor)
{
    int gl_major, gl_minor;
    getGlVersion(&gl_major, &gl_minor);

    *major = *minor = 0;
    if(gl_major == 1) {
        /* GL v1.x can only provide GLSL v1.00 as an extension */
        const char *extstr = (const char *) glGetString(GL_EXTENSIONS);
        if ((extstr != NULL) &&
            (strstr(extstr, "GL_ARB_shading_language_100") != NULL)) {
            *major = 1;
            *minor = 0;
        }
    }
    else if (gl_major >= 2)
    {
        /* GL v2.0 and greater must parse the version string */
        const char *verstr =
        (const char *) glGetString(GL_SHADING_LANGUAGE_VERSION);

        if ((verstr == NULL) ||
            (std::sscanf(verstr, "%d.%d", major, minor) != 2)) {
            *major = *minor = 0;
            //fprintf(stderr, "Invalid GL_SHADING_LANGUAGE_VERSION format!!!\n");
        }
    }
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
*/
void
TestOpenGLPlugin::contextAttached()
{
#ifdef DEBUG
    DPRINT(("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER)));
    DPRINT(("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION)));
    DPRINT(("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR)));
    DPRINT(("GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS)));
#endif
    // Non-power-of-two textures are supported if the GL version is 2.0 or greater, or if the implementation exports the GL_ARB_texture_non_power_of_two extension. (Mesa does, of course)
    int major, minor;
    getGlVersion(&major, &minor);
    if (major < 2) {
        if (!glutExtensionSupported("GL_ARB_texture_non_power_of_two")) {
            sendMessage(OFX::Message::eMessageError, "", "Can not render: OpenGL 2.0 or GL_ARB_texture_non_power_of_two is required.");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (major < 3) {
    }
    _haveAniso = glutExtensionSupported("GL_EXT_texture_filter_anisotropic");
    if (_haveAniso) {
        GLfloat MaxAnisoMax;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &MaxAnisoMax);
        _maxAnisoMax = MaxAnisoMax;
        DPRINT(("GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT = %f\n", _maxAnisoMax));
    }
}

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
TestOpenGLPlugin::contextDetached()
{
}
