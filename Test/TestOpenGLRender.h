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
    int srcWidth = 1;
    int srcHeight = 1;
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
    const GLenum srcTarget = GL_TEXTURE_RECTANGLE_NV;
    OfxRectI srcBounds = src->getBounds();
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glBindTexture(srcTarget, srcIndex);
    int srcWidth = srcBounds.x2 - srcBounds.x1;
    int srcHeight = srcBounds.y2 - srcBounds.y1;

    glTexImage2D(srcTarget, 0, format,
                 srcWidth, srcHeight, 0,
                 format, type, src->getPixelData());

    // setup the projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(dstBounds.x1, dstBounds.x2,
            dstBounds.y1, dstBounds.y2,
            -10.0, 10.0);
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
    glTexCoord2f (0 * srcWidth, tymin * srcHeight);
    glVertex2f   (0, 0);
    glTexCoord2f (1 * srcWidth, tymin * srcHeight);
    glVertex2f   (w * sourceScale, 0);
    glTexCoord2f (1 * srcWidth, tymax * srcHeight);
    glVertex2f   (w * sourceScale, h * sourceScale);
    glTexCoord2f (0 * srcWidth, tymax * srcHeight);
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
