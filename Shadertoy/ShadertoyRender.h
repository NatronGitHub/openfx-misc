/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2016 INRIA
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
 * OFX Shadertoy plugin.
 */

#include "Shadertoy.h"

#include <cstring> // strstr, strchr, strlen
#include <cstdio> // sscanf, vsnprintf, fwrite

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
#  define contextAttached contextAttachedMesa
#  define contextDetached contextDetachedMesa
#else
#  define RENDERFUNC renderGL
#endif

#if defined(HAS_GLES)
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cassert>
#define TO_STRING(...) #__VA_ARGS__
#else
#ifdef __APPLE__
#  include <OpenGL/gl.h>
#  include <OpenGL/glu.h>
#else
#  include <GL/gl.h>
#  include <GL/glu.h>
#endif
#endif

#ifndef DEBUG
#define DPRINT(args) (void)0
#define glCheckError() ( (void)0 )
#else
#include <cstdarg> // ...
#include <iostream>
#include <stdio.h> // for snprintf & _snprintf
#ifdef _WINDOWS
#  include <windows.h>
#  if defined(_MSC_VER) && _MSC_VER < 1900
#    define snprintf _snprintf
#  endif
#endif // _WINDOWS

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
ShadertoyPlugin::RENDERFUNC(const OFX::RenderArguments &args)
{
    const double time = args.time;
    std::string imageShader;
    bool mipmap = true;
    bool anisotropic = true;
    if (_imageShader) {
        _imageShader->getValueAtTime(time, imageShader);
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
    std::auto_ptr<const OFX::Texture> src[4];
    for (unsigned i = 0; i< 4; ++i) {
        src[i].reset((_srcClips[i] && _srcClips[i]->isConnected()) ?
                     _srcClips[i]->loadTexture(time) : 0);
    }
# else
    std::auto_ptr<const OFX::Image> src[4];
    for (unsigned i = 0; i< 4; ++i) {
        src[i].reset((_srcClips[0] && _srcClips[0]->isConnected()) ?
                     _srcClips[0]->fetchImage(time) : 0);
    }
# endif

    std::vector<OFX::BitDepthEnum> srcBitDepth(4, OFX::eBitDepthNone);
    std::vector<OFX::PixelComponentEnum> srcComponents(4, OFX::ePixelComponentNone);
# ifdef USE_OPENGL
    std::vector<GLuint> srcIndex(4);
    std::vector<GLenum> srcTarget(4);
# endif
#ifdef USE_OSMESA
    GLenum format = 0;
    GLint depthBits = 0;
    GLint stencilBits = 0;
    GLint accumBits = 0;
    GLenum type = 0;
#endif

    for (unsigned i = 0; i< 4; ++i) {
        if (src[i].get()) {
            srcBitDepth[i] = src[i]->getPixelDepth();
            srcComponents[i] = src[i]->getPixelComponents();
            if (srcBitDepth[i] != dstBitDepth || srcComponents[i] != dstComponents) {
                OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
                return;
            }
# ifdef USE_OPENGL
            srcIndex[i] = (GLuint)src[i]->getIndex();
            srcTarget[i] = (GLenum)src[i]->getTarget();
            DPRINT(("openGL: source texture %u index %d, target %d, depth %s\n",
                    i, srcIndex[i], srcTarget[i], mapBitDepthEnumToStr(srcBitDepth[i])));
# endif
            // XXX: check status for errors

#ifdef USE_OSMESA
            GLenum formati;
            switch (srcComponents[i]) {
                case OFX::ePixelComponentRGBA:
                    formati = GL_RGBA;
                    break;
                case OFX::ePixelComponentAlpha:
                    formati = GL_ALPHA;
                    break;
                default:
                    OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
                    return;
            }
            GLint depthBitsi = 0;
            GLint stencilBitsi = 0;
            GLint accumBitsi = 0;
            GLenum typei;
            switch (srcBitDepth[i]) {
                case OFX::eBitDepthUByte:
                    depthBitsi = 16;
                    typei = GL_UNSIGNED_BYTE;
                    break;
                case OFX::eBitDepthUShort:
                    depthBitsi = 16;
                    typei = GL_UNSIGNED_SHORT;
                    break;
                case OFX::eBitDepthFloat:
                    depthBitsi = 32;
                    typei = GL_FLOAT;
                    break;
                default:
                    OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
                    return;
            }
            if (format == 0) {
                format = formati;
                depthBits = depthBitsi;
                stencilBits = stencilBitsi;
                accumBits = accumBitsi;
                type = typei;
            } else {
                if (format != formati ||
                    depthBits != depthBitsi ||
                    stencilBits != stencilBitsi ||
                    accumBits != accumBitsi ||
                    type != typei) {
                    OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
                }
            }
#endif
        }
    }

#ifdef USE_OSMESA
    if (format == 0) {
        switch (dstComponents) {
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
    }
    if (depthBits == 0) {
        switch (dstBitDepth) {
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
    contextAttachedMesa();

    // load the source image into a texture
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Non-power-of-two textures are supported if the GL version is 2.0 or greater, or if the implementation exports the GL_ARB_texture_non_power_of_two extension. (Mesa does, of course)

    std::vector<GLenum> srcTarget(4, GL_TEXTURE_2D);
    std::vector<GLuint> srcIndex(4);
    for (unsigned i = 0; i< 4; ++i) {
        if (src[i].get()) {
            glGenTextures(1, &srcIndex[i]);
            OfxRectI srcBounds = src[i]->getBounds();
            glActiveTextureARB(GL_TEXTURE0_ARB);
            glBindTexture(srcTarget[i], srcIndex[i]);
            if (mipmap) {
                // this must be done before glTexImage2D
                glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
                // requires extension GL_SGIS_generate_mipmap or OpenGL 1.4.
                glTexParameteri(srcTarget[i], GL_GENERATE_MIPMAP, GL_TRUE); // Allocate the mipmaps
            }

            glTexImage2D(srcTarget[i], 0, format,
                         srcBounds.x2 - srcBounds.x1, srcBounds.y2 - srcBounds.y1, 0,
                         format, type, src[i]->getPixelData());
        }
    }
    // setup the projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(dstBounds.x1, dstBounds.x2,
            dstBounds.y1, dstBounds.y2,
            -10.0*(dstBounds.y2-dstBounds.y1), 10.0*(dstBounds.y2-dstBounds.y1));
    glMatrixMode(GL_MODELVIEW);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    
#endif

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

    // set up textures (how much of this is needed?)
    for (unsigned i = 0; i < 4; ++i) {
        glEnable(srcTarget[i]);
        glBindTexture(srcTarget[i], srcIndex[i]);
        glTexParameteri(srcTarget[i], GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(srcTarget[i], GL_TEXTURE_WRAP_T, GL_REPEAT);
        //glTexParameteri(srcTarget[i], GL_TEXTURE_BASE_LEVEL, 0);
        //glTexParameteri(srcTarget[i], GL_TEXTURE_MAX_LEVEL, 1);
        //glTexParameterf(srcTarget[i], GL_TEXTURE_MIN_LOD, -1);
        //glTexParameterf(srcTarget[i], GL_TEXTURE_MAX_LOD, 1);
        glTexParameteri(srcTarget[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // With opengl render, we don't know if mipmaps were generated by the host.
        // check if mipmaps exist for that texture (we only check if level 1 exists)
        {
            int width = 0;
            glGetTexLevelParameteriv(srcTarget[i], 1, GL_TEXTURE_WIDTH, &width);
            if (width == 0) {
                mipmap = false;
            }
        }
        glTexParameteri(srcTarget[i], GL_TEXTURE_MIN_FILTER, mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        if (anisotropic && _haveAniso) {
            glTexParameterf(srcTarget[i], GL_TEXTURE_MAX_ANISOTROPY_EXT, _maxAnisoMax);
        }
        glDisable(srcTarget[i]);
    }
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    // textures are oriented with Y up (standard orientation)
    //float tymin = 0;
    //float tymax = 1;

    // now draw the textured quad containing the source
    const double scale=0.333;
    glEnable(srcTarget[0]);
    glBegin(GL_QUADS);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin (GL_QUADS);
    glTexCoord2f (0, 0);
    glVertex2f   (0, 0);
    glTexCoord2f (1, 0);
    glVertex2f   (w * scale, 0);
    glTexCoord2f (1, 1);
    glVertex2f   (w * scale, h * scale);
    glTexCoord2f (0, 1);
    glVertex2f   (0, h * scale);
    glEnd ();
    glDisable(srcTarget[0]);

    // done; clean up.
    glPopAttrib();

#ifdef USE_OSMESA
    /* This is very important!!!
     * Make sure buffered commands are finished!!!
     */
    glFinish();
    for (unsigned i = 0; i < 4; ++i) {
        glDeleteTextures(1, &srcIndex[i]);
    }

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

    contextDetachedMesa();
    /* destroy the context */
    OSMesaDestroyContext( ctx );
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
ShadertoyPlugin::contextAttached()
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

    // Shadertoy:
#if defined(HAS_GLES)
    static const GLfloat vertex_data[] = {
        -1.0,1.0,1.0,1.0,
        1.0,1.0,1.0,1.0,
        1.0,-1.0,1.0,1.0,
        -1.0,-1.0,1.0,1.0,
    };
    glGetError();
    // Upload vertex data to a buffer
    GLuint vertex_buffer;
    glGenBuffers(1, &vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data, GL_STATIC_DRAW);
    _vertex_buffer = vertex_buffer;
#endif
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
ShadertoyPlugin::contextDetached()
{
    // Shadertoy:
#if defined(HAS_GLES)
    GLuint vertex_buffer = _vertex_buffer;
    glDeleteBuffers(1, &vertex_buffer);
    _vertex_buffer = 0;
#endif
}
