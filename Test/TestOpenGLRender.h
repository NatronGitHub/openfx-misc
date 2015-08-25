/*
 OFX TestOpenGL plugin.

 Copyright (C) 2015 INRIA

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France
 */

#include "TestOpenGL.h"

// first, check that the file is used in a good way
#if !defined(USE_OPENGL) && !defined(USE_OSMESA)
#error "USE_OPENGL or USE_OSMESA must be defined before including this file."
#endif
#if defined(USE_OPENGL) && defined(USE_OSMESA)
#error "include this file first only once, either with USE_OPENGL, or with USE_OSMESA"
#endif

#ifdef USE_OSMESA
#  include <GL/gl_mangle.h>
#  include <GL/glu_mangle.h>
#  include <GL/osmesa.h>
#  define RENDERFUNC renderMesa
#else
#  define RENDERFUNC renderGL
#endif

#ifdef __APPLE__
#  include <OpenGL/gl.h>
#  include <OpenGL/glu.h>
#else
#  include <GL/gl.h>
#  include <GL/glu.h>
#endif

#ifndef DEBUG
#define DPRINT(args) (void)0
#else
#include <cstdio> // vsnprintf, fwrite
#include <cstdarg> // ...
#include <cstring> // strlen

#define DPRINT(args) print_dbg args
static
void print_dbg(const char *fmt, ...)
{
    char msg[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, 1023, fmt, ap);
    fwrite(msg, sizeof(char), strlen(msg), stderr);
    fflush(stderr);
#ifdef _WIN32
    OutputDebugString(msg);
#endif
    va_end(ap);
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

void
TestOpenGLPlugin::RENDERFUNC(const OFX::RenderArguments &args)
{
    const double time = args.time;

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
    DPRINT(("Render: window = [%d, %d - %d, %d]\n",
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
    /* Create an RGBA-mode context */
#if OSMESA_MAJOR_VERSION * 100 + OSMESA_MINOR_VERSION >= 305
    /* specify Z, stencil, accum sizes */
    OSMesaContext ctx = OSMesaCreateContextExt( format, depthBits, stencilBits, accumBits, NULL );
#else
    OSMesaContext ctx = OSMesaCreateContext( format, NULL );
#endif
    if (!ctx) {
        DPRINT(("OSMesaCreateContext failed!\n"));
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    /* Allocate the image buffer */
    void* buffer = dst->getPixelData();
    OfxRectI dstBounds = dst->getBounds();
    /* Bind the buffer to the context and make it current */
    if (!OSMesaMakeCurrent( ctx, buffer, type, dstBounds.x2 - dstBounds.x1, dstBounds.y2 - dstBounds.y1 )) {
        DPRINT(("OSMesaMakeCurrent failed!\n"));
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // load the source image into a texture
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLuint srcIndex;
    glGenTextures(1, &srcIndex);
    // Non-power-of-two textures are supported if the GL version is 2.0 or greater, or if the implementation exports the GL_ARB_texture_non_power_of_two extension. (Mesa does, of course)

    const GLenum srcTarget = GL_TEXTURE_2D;
    OfxRectI srcBounds = src->getBounds();
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glBindTexture(srcTarget, srcIndex);

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

    
#endif

#ifdef DEBUG
    DPRINT(("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER)));
    DPRINT(("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION)));
    DPRINT(("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR)));
    DPRINT(("GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS)));
#endif
    // get the scale parameter
    double scale = 1;
    double sourceScale = 1;
    if (_scale) {
        _scale->getValueAtTime(time, scale);
    }
    if (_sourceScale) {
        _sourceScale->getValueAtTime(time, sourceScale);
    }

    const OfxPointD& rs = args.renderScale;

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
    glTexParameteri(srcTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(srcTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    // textures are oriented with Y up (standard orientation)
    float tymin = 0;
    float tymax = 1;

    // now draw the textured quad containing the source
    glBegin(GL_QUADS);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin (GL_QUADS);
    glTexCoord2f (0, tymin);
    glVertex2f   (0, 0);
    glTexCoord2f (1, tymin);
    glVertex2f   (w * sourceScale, 0);
    glTexCoord2f (1, tymax);
    glVertex2f   (w * sourceScale, h * sourceScale);
    glTexCoord2f (0, tymax);
    glVertex2f   (0, h * sourceScale);
    glEnd ();

    glDisable(srcTarget);

    // Now draw some stuff on top of it to show we really did something
#define WIDTH 200
#define HEIGHT 100
    glBegin(GL_QUADS);
    glColor3f(1.0f, 0, 0); //Set the colour to red
    glVertex2f(10 * rs.x, 10 * rs.y);
    glVertex2f(10 * rs.x, (10 + HEIGHT * scale) * rs.y);
    glVertex2f((10 + WIDTH * scale) * rs.x, (10 + HEIGHT * scale) * rs.y);
    glVertex2f((10 + WIDTH * scale) * rs.x, 10 * rs.y);
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
    glutSolidTeapot(h/4.);
    glDisable(srcTarget);
    glPopMatrix();

    // done; clean up.
    glPopAttrib();

#ifdef USE_OSMESA
    /* This is very important!!!
     * Make sure buffered commands are finished!!!
     */
    glFinish();

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

    /* destroy the context */
    OSMesaDestroyContext( ctx );
#endif
}
