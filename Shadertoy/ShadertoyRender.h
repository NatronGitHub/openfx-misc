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
#include <cassert>

#include "ofxsMacros.h"
#include "ofxsMultiThread.h"
#include "ofxsCoords.h"

// first, check that the file is used in a good way
#if !defined(USE_OPENGL) && !defined(USE_OSMESA)
#error "USE_OPENGL or USE_OSMESA must be defined before including this file."
#endif
#if defined(USE_OPENGL) && defined(USE_OSMESA)
#error "include this file first only once, either with USE_OPENGL, or with USE_OSMESA"
#endif


#if defined(USE_OSMESA) || !defined(_WINDOWS)
#define GL_GLEXT_PROTOTYPES
#endif

#ifdef USE_OSMESA
#  include <GL/gl_mangle.h>
#  include <GL/glu_mangle.h>
#  include <GL/osmesa.h>
#  define RENDERFUNC renderMesa
#  define contextAttached contextAttachedMesa
#  define contextDetached contextDetachedMesa
#  define ShadertoyShader ShadertoyShaderMesa // in case OpenGL and Mesa use different type definitions
#else
#  define RENDERFUNC renderGL
#  define ShadertoyShader ShadertoyShaderOpenGL
#endif

#if !defined(USE_OSMESA) && defined(__APPLE__)
#  include <OpenGL/gl.h>
#  include <OpenGL/glext.h>
#  include <OpenGL/glu.h>
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
#  include <GL/glu.h>
#endif

#define NBINPUTS SHADERTOY_NBINPUTS

struct ShadertoyShader {
    ShadertoyShader()
    : program(0)
    , iResolutionLoc(-1)
    , iGlobalTimeLoc(-1)
    , iTimeDeltaLoc(-1)
    , iFrameLoc(-1)
    , iChannelTimeLoc(-1)
    , iMouseLoc(-1)
    , iDateLoc(-1)
    , iSampleRateLoc(-1)
    , iChannelResolutionLoc(-1)
    , ifFragCoordOffsetUniform(-1)
    {
        std::fill(iChannelLoc, iChannelLoc+NBINPUTS, -1);
    }

    GLuint program;
    GLint iResolutionLoc;
    GLint iGlobalTimeLoc;
    GLint iTimeDeltaLoc;
    GLint iFrameLoc;
    GLint iChannelTimeLoc;
    GLint iMouseLoc;
    GLint iDateLoc;
    GLint iSampleRateLoc;
    GLint iChannelResolutionLoc;
    GLint ifFragCoordOffsetUniform;
    GLint iChannelLoc[4];
};

#if !defined(USE_MESA) && defined(_WINDOWS)
// Program
static PFNGLCREATEPROGRAMPROC glCreateProgram = NULL;
static PFNGLDELETEPROGRAMPROC glDeleteProgram = NULL;
static PFNGLUSEPROGRAMPROC glUseProgram = NULL;
static PFNGLATTACHSHADERPROC glAttachShader = NULL;
static PFNGLDETACHSHADERPROC glDetachShader = NULL;
static PFNGLLINKPROGRAMPROC glLinkProgram = NULL;
static PFNGLGETPROGRAMIVPROC glGetProgramiv = NULL;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = NULL;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
static PFNGLUNIFORM1IPROC glUniform1i = NULL;
static PFNGLUNIFORM1IVPROC glUniform1iv = NULL;
static PFNGLUNIFORM2IVPROC glUniform2iv = NULL;
static PFNGLUNIFORM3IVPROC glUniform3iv = NULL;
static PFNGLUNIFORM4IVPROC glUniform4iv = NULL;
static PFNGLUNIFORM1FPROC glUniform1f = NULL;
static PFNGLUNIFORM1FVPROC glUniform1fv = NULL;
static PFNGLUNIFORM2FVPROC glUniform2fv = NULL;
static PFNGLUNIFORM3FVPROC glUniform3fv = NULL;
static PFNGLUNIFORM4FVPROC glUniform4fv = NULL;
static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = NULL;
static PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation = NULL;
static PFNGLVERTEXATTRIB1FPROC glVertexAttrib1f = NULL;
static PFNGLVERTEXATTRIB1FVPROC glVertexAttrib1fv = NULL;
static PFNGLVERTEXATTRIB2FVPROC glVertexAttrib2fv = NULL;
static PFNGLVERTEXATTRIB3FVPROC glVertexAttrib3fv = NULL;
static PFNGLVERTEXATTRIB4FVPROC glVertexAttrib4fv = NULL;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = NULL;
static PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = NULL;
static PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation = NULL;
static PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform = NULL;

// Shader
static PFNGLCREATESHADERPROC glCreateShader = NULL;
static PFNGLDELETESHADERPROC glDeleteShader = NULL;
static PFNGLSHADERSOURCEPROC glShaderSource = NULL;
static PFNGLCOMPILESHADERPROC glCompileShader = NULL;
static PFNGLGETSHADERIVPROC glGetShaderiv = NULL;

// VBO
static PFNGLGENBUFFERSPROC glGenBuffers = NULL;
static PFNGLBINDBUFFERPROC glBindBuffer = NULL;
static PFNGLBUFFERDATAPROC glBufferData = NULL;
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = NULL;

//Multitexture
static PFNGLACTIVETEXTUREARBPROC glActiveTexture = NULL;
static PFNGLCLIENTACTIVETEXTUREPROC glClientActiveTexture = NULL;
static PFNGLMULTITEXCOORD2FPROC glMultiTexCoord2f = NULL;

// Framebuffers
//static PFNGLISFRAMEBUFFERPROC glIsFramebuffer = NULL;
static PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = NULL;
static PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = NULL;
static PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = NULL;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = NULL;
//PFNGLFRAMEBUFFERTEXTURE1DPROC glFramebufferTexture1D = NULL;
static PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = NULL;
//PFNGLFRAMEBUFFERTEXTURE3DPROC glFramebufferTexture3D = NULL;
//PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer = NULL;
//PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC glGetFramebufferAttachmentParameteriv = NULL;
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


#ifdef USE_OSMESA
struct ShadertoyPlugin::OSMesaPrivate
{
    OSMesaPrivate(ShadertoyPlugin *effect)
    : _effect(effect)
    , _ctx(0)
    , _ctxFormat(0)
    , _ctxDepthBits(0)
    , _ctxStencilBits(0)
    , _ctxAccumBits(0)
    , _imageShaderID(0)
    , _imageShader()
    {
    }

    ~OSMesaPrivate() {
        /* destroy the context */
        if (_ctx) {
            _effect->contextDetachedMesa();
            OSMesaDestroyContext( _ctx );
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

        if (!_ctx || (format      != _ctxFormat &&
                      depthBits   != _ctxDepthBits &&
                      stencilBits != _ctxStencilBits &&
                      accumBits   != _ctxAccumBits)) {
            /* destroy the context */
            if (_ctx) {
                _effect->contextDetachedMesa();
                OSMesaDestroyContext( _ctx );
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

        /* Bind the buffer to the context and make it current */
        if (!OSMesaMakeCurrent( _ctx, buffer, type, dstBounds.x2 - dstBounds.x1, dstBounds.y2 - dstBounds.y1 )) {
            DPRINT(("OSMesaMakeCurrent failed!\n"));
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
        if (newContext) {
            _effect->contextAttachedMesa();
        } else {
            // set viewport
            glViewport(0, 0, dstBounds.x2 - dstBounds.x1, dstBounds.y2 - dstBounds.y1);
        }
    }

    OSMesaContext ctx() { return _ctx; }

    ShadertoyPlugin *_effect;
    // information about the current Mesa context
    OSMesaContext _ctx;
    GLenum _ctxFormat;
    GLint _ctxDepthBits;
    GLint _ctxStencilBits;
    GLint _ctxAccumBits;
    unsigned int _imageShaderID; // the shader ID compiled for this context
    ShadertoyShader _imageShader;
};

void
ShadertoyPlugin::initMesa()
{
}

void
ShadertoyPlugin::exitMesa()
{
    OFX::MultiThread::AutoMutex lock(_osmesaMutex);
    for (std::list<OSMesaPrivate *>::iterator it = _osmesa.begin(); it != _osmesa.end(); ++it) {
        // make the context current, with a dummy buffer
        unsigned char buffer[4];
        OSMesaMakeCurrent((*it)->ctx(), buffer, GL_UNSIGNED_BYTE, 1, 1);
        contextDetachedMesa();
        delete *it;
    }
    _osmesa.clear();
}

#endif // USE_MESA


#ifdef USE_OPENGL

void
ShadertoyPlugin::initOpenGL()
{
    _imageShader = new ShadertoyShader;
}

void
ShadertoyPlugin::exitOpenGL()
{
    delete _imageShader;
}

#endif // USE_OPENGL

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


static
GLuint compileShader(GLenum shaderType, const char *shader, std::string &errstr)
{
    GLuint s = glCreateShader(shaderType);
    if (s == 0) {
        DPRINT(("Failed to create shader from\n====\n%s\n===\n", shader));

        return 0;
    }

    glShaderSource(s, 1, &shader, NULL);
    glCompileShader(s);

    GLint param;
    glGetShaderiv(s, GL_COMPILE_STATUS, &param);
    if (param != GL_TRUE) {
        errstr = "Failed to compile ";
        errstr += (shaderType == GL_VERTEX_SHADER) ? "vertex" : "fragment";
        errstr += " shader source!\n";
        //errstr += "\n====\n";
        //errstr += shader;
        //errstr += "\n===\n";


        int infologLength = 0;
        char *infoLog;

        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &infologLength);

        if (infologLength > 0) {
            infoLog = new char[infologLength];
            glGetShaderInfoLog(s, infologLength, NULL, infoLog);
            if (shaderType == GL_FRAGMENT_SHADER) {
                errstr += "\nError log (subtract 10 to line numbers):\n";
            } else {
                errstr += "\nError log:\n";
            }
            errstr += infoLog;
            delete [] infoLog;
        } else {
            errstr += "(no error log)";
        }

        glDeleteShader(s);
        DPRINT(("%s\n", errstr.c_str()));

        return 0;
    }

    return s;
}

static
GLuint compileAndLinkProgram(const char *vertexShader, const char *fragmentShader, std::string &errstr)
{
    DPRINT(("CompileAndLink\n"));
    GLuint program = glCreateProgram();
    if (program == 0) {
        DPRINT(("Failed to create program\n"));
        glCheckError();
        return program;
    }

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShader, errstr);
    if (!vs) {
        glDeleteProgram(program);
        return 0;
    }
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShader, errstr);
    if (!fs) {
        glDeleteShader(vs);
        glDeleteProgram(program);
        return 0;
    }

    if (vs && fs) {
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        GLint param;
        glGetProgramiv(program, GL_LINK_STATUS, &param);
        if (param != GL_TRUE) {
            errstr = "Failed to link shader program\n";
            glCheckError();
            glGetError();
            int infologLength = 0;
            char *infoLog;

            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infologLength);

            if (infologLength > 0) {
                infoLog = new char[infologLength];
                glGetProgramInfoLog(program, infologLength, NULL, infoLog);
                errstr += "\nError Log:\n";
                errstr += infoLog;
                delete [] infoLog;
            } else {
                errstr += "(no error log)";
            }
            //errstr += "\n==== Vertex shader source:\n";
            //errstr += vertexShader;
            //errstr += "\n==== Fragment shader source:\n";
            //errstr += fragmentShader;

            glDetachShader(program, vs);
            glDeleteShader(vs);
            
            glDetachShader(program, fs);
            glDeleteShader(fs);
            
            glDeleteProgram(program);
            DPRINT(("%s\n", errstr.c_str()));

            return 0;
        }
    } else {
        glDeleteProgram(program);
    }

    if (vs)
        glDeleteShader(vs);
    
    if (fs)
        glDeleteShader(fs);
    
    return program;
}

// https://raw.githubusercontent.com/beautypi/shadertoy-iOS-v2/master/shadertoy/shaders/vertex_main.glsl
/*
precision highp float;
precision highp int;

attribute vec3 position;

void main() {
    gl_Position.xyz = position;
    gl_Position.w = 1.0;
}

 */
static std::string vsSource = "void main() { gl_Position = ftransform(); }";

// https://raw.githubusercontent.com/beautypi/shadertoy-iOS-v2/master/shadertoy/shaders/fragment_base_uniforms.glsl
/*
#extension GL_EXT_shader_texture_lod : enable
#extension GL_OES_standard_derivatives : enable

precision highp float;
precision highp int;
precision mediump sampler2D;

uniform vec3      iResolution;                  // viewport resolution (in pixels)
uniform float     iGlobalTime;                  // shader playback time (in seconds)
uniform vec4      iMouse;                       // mouse pixel coords
uniform vec4      iDate;                        // (year, month, day, time in seconds)
uniform float     iSampleRate;                  // sound sample rate (i.e., 44100)
uniform vec3      iChannelResolution[4];        // channel resolution (in pixels)
uniform float     iChannelTime[4];              // channel playback time (in sec)

uniform vec2      ifFragCoordOffsetUniform;     // used for tiled based hq rendering
uniform float     iTimeDelta;                   // render time (in seconds)
uniform int       iFrame;                       // shader playback frame
*/
static std::string fsHeader =
//"#extension GL_OES_standard_derivatives : enable\n"
//"precision mediump float;\n"
//"precision mediump int;\n"
"uniform vec3      iResolution;\n"
"uniform float     iGlobalTime;\n"
"uniform float     iTimeDelta;\n"
"uniform int       iFrame;\n"
"uniform float     iChannelTime["STRINGISE(NBINPUTS)"];\n"
"uniform vec3      iChannelResolution["STRINGISE(NBINPUTS)"];\n"
"uniform vec4      iMouse;\n"
"uniform vec4      iDate;\n"
"uniform float     iSampleRate;\n"
"uniform vec2      ifFragCoordOffsetUniform;\n"
;

// https://raw.githubusercontent.com/beautypi/shadertoy-iOS-v2/master/shadertoy/shaders/fragment_main_image.glsl
/*

void main()  {
    mainImage(gl_FragColor, gl_FragCoord.xy + ifFragCoordOffsetUniform );
}
*/
static std::string fsFooter =
"void main(void)\n"
"{\n"
"  mainImage(gl_FragColor, gl_FragCoord.xy + ifFragCoordOffsetUniform );\n"
//"  vec4 color = vec4(0.0, 0.0, 0.0, 1.0);\n"
//"  mainImage(color, gl_FragCoord.xy + ifFragCoordOffsetUniform);\n"
//"  color.w = 1.0;\n"
//"  gl_FragColor = color;\n"
"}\n";

void
ShadertoyPlugin::RENDERFUNC(const OFX::RenderArguments &args)
{
    const double time = args.time;
    bool mipmap = true;
    bool anisotropic = true;
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
    std::auto_ptr<const OFX::Texture> src[NBINPUTS];
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        src[i].reset((_srcClips[i] && _srcClips[i]->isConnected()) ?
                     _srcClips[i]->loadTexture(time) : 0);
    }
# else
    std::auto_ptr<const OFX::Image> src[NBINPUTS];
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        src[i].reset((_srcClips[i] && _srcClips[i]->isConnected()) ?
                     _srcClips[i]->fetchImage(time) : 0);
    }
# endif

    std::vector<OFX::BitDepthEnum> srcBitDepth(NBINPUTS, OFX::eBitDepthNone);
    std::vector<OFX::PixelComponentEnum> srcComponents(NBINPUTS, OFX::ePixelComponentNone);
# ifdef USE_OPENGL
    std::vector<GLuint> srcIndex(NBINPUTS);
    std::vector<GLenum> srcTarget(NBINPUTS);
# endif
#ifdef USE_OSMESA
    GLenum format = 0;
    GLint depthBits = 0;
    GLint stencilBits = 0;
    GLint accumBits = 0;
    GLenum type = 0;
#endif

    for (unsigned i = 0; i < NBINPUTS; ++i) {
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
                    // all inputs should have the same format
                    OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
                }
            }
#endif
        }
    }

    const OfxRectI dstBounds = dst->getBounds();
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
    /* Allocate the image buffer */
    void* buffer = dst->getPixelData();
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
    osmesa->setContext(format, depthBits, type, stencilBits, accumBits, buffer, dstBounds);


    // load the source image into a texture
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Non-power-of-two textures are supported if the GL version is 2.0 or greater, or if the implementation exports the GL_ARB_texture_non_power_of_two extension. (Mesa does, of course)

    std::vector<GLenum> srcTarget(4, GL_TEXTURE_2D);
    std::vector<GLuint> srcIndex(NBINPUTS);
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        if (src[i].get()) {
            glGenTextures(1, &srcIndex[i]);
            OfxRectI srcBounds = src[i]->getBounds();
            glActiveTextureARB(GL_TEXTURE0_ARB + i);
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
    glOrtho(0, dstBounds.x2 - dstBounds.x1,
            0, dstBounds.y2 - dstBounds.y1,
            -10.0*(dstBounds.y2-dstBounds.y1), 10.0*(dstBounds.y2-dstBounds.y1));
    glMatrixMode(GL_MODELVIEW);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );


#endif

    // compile and link the shader if necessary
    ShadertoyShader *shadertoy;
    {
        OFX::MultiThread::AutoMutex lock(_shaderMutex);
        bool must_recompile = false;
#ifdef USE_OPENGL
        shadertoy = _imageShader;
        assert(shadertoy);
        must_recompile = _imageShaderChanged;
        _imageShaderChanged = false;
#endif
#ifdef USE_OSMESA
        shadertoy = &osmesa->_imageShader;
        must_recompile = (_imageShaderID != osmesa->_imageShaderID);
        osmesa->_imageShaderID = _imageShaderID;
#endif
        assert(shadertoy);
        if (must_recompile) {
            if (shadertoy->program) {
                glDeleteProgram(shadertoy->program);
                shadertoy->program = 0;
            }
            std::string str;
            _imageShaderSource->getValue(str);
            std::string fsSource = fsHeader;
            for (unsigned i = 0; i < NBINPUTS; ++i) {
                fsSource += std::string("uniform sampler2D iChannel") + (char)('0'+i) + ";\n";
            }
            fsSource += '\n' + str + '\n' + fsFooter;
            std::string errstr;
            shadertoy->program = compileAndLinkProgram(vsSource.c_str(), fsSource.c_str(), errstr);
            if (shadertoy->program == 0) {
                setPersistentMessage(OFX::Message::eMessageError, "", "Failed to compile and link program");
                sendMessage(OFX::Message::eMessageError, "", errstr.c_str());
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
           }
            _imageShaderChanged = false;
            shadertoy->iResolutionLoc        = glGetUniformLocation(shadertoy->program, "iResolution");
            shadertoy->iGlobalTimeLoc        = glGetUniformLocation(shadertoy->program, "iGlobalTime");
            shadertoy->iTimeDeltaLoc         = glGetUniformLocation(shadertoy->program, "iTimeDelta");
            shadertoy->iFrameLoc             = glGetUniformLocation(shadertoy->program, "iFrame");
            shadertoy->iChannelTimeLoc       = glGetUniformLocation(shadertoy->program, "iChannelTime");
            shadertoy->iMouseLoc             = glGetUniformLocation(shadertoy->program, "iMouse");
            shadertoy->iDateLoc              = glGetUniformLocation(shadertoy->program, "iDate");
            shadertoy->iSampleRateLoc        = glGetUniformLocation(shadertoy->program, "iSampleRate");
            shadertoy->iChannelResolutionLoc = glGetUniformLocation(shadertoy->program, "iChannelResolution");
            shadertoy->ifFragCoordOffsetUniform = glGetUniformLocation(shadertoy->program, "ifFragCoordOffsetUniform");
            char iChannelX[10] = "iChannelX"; // index 8 holds the channel character
            assert(NBINPUTS < 10 && iChannelX[8] == 'X');
            for (unsigned i = 0; i < NBINPUTS; ++i) {
                iChannelX[8] = '0' + i;
                shadertoy->iChannelLoc[i] = glGetUniformLocation (shadertoy->program, iChannelX);
            }
        }
    }
    //GLuint shadertoy_shader = shadertoy->program;

    double fps = _dstClip->getFrameRate();
    if (fps <= 0) {
        fps = 1.;
    }
    GLfloat t = time / fps;
    const OfxPointD& rs = args.renderScale;
    OfxRectI dstBoundsFull;
    OFX::Coords::toPixelEnclosing(_dstClip->getRegionOfDefinition(time), rs, _dstClip->getPixelAspectRatio(), &dstBoundsFull);

    glUseProgram(shadertoy->program);

    // Uniform locations may be -1 if the Uniform was optimised out by the compîler.
    // see https://www.opengl.org/wiki/GLSL_:_common_mistakes#glGetUniformLocation_and_glGetActiveUniform
    if (shadertoy->iResolutionLoc >= 0) {
        double width = dstBoundsFull.x2 - dstBoundsFull.x1;
        double height = dstBoundsFull.y2 - dstBoundsFull.y1;
        // last coord is 1.
        // see https://github.com/beautypi/shadertoy-iOS-v2/blob/a852d8fd536e0606377a810635c5b654abbee623/shadertoy/ShaderPassRenderer.m#L329
        glUniform3f (shadertoy->iResolutionLoc, width, height, 1.);
    }
    if (shadertoy->iGlobalTimeLoc >= 0) {
        glUniform1f (shadertoy->iGlobalTimeLoc, t);
    }
    if (shadertoy->iTimeDeltaLoc >= 0) {
        glUniform1f (shadertoy->iTimeDeltaLoc, 1 / fps); // is that it?
    }
    if (shadertoy->iFrameLoc >= 0) {
        glUniform1f (shadertoy->iFrameLoc, time); // is that it?
    }
    if (shadertoy->iChannelTimeLoc >= 0) {
        GLfloat tv[NBINPUTS];
        std::fill(tv, tv + NBINPUTS, t);
        glUniform1fv(shadertoy->iChannelTimeLoc, NBINPUTS, tv);
    }
    if (shadertoy->iChannelResolutionLoc >= 0) {
        GLfloat rv[3*NBINPUTS];
        for (unsigned i = 0; i < NBINPUTS; ++i) {
        }
        glUniform3fv(shadertoy->iChannelResolutionLoc, NBINPUTS, rv);
    }
    if (shadertoy->iMouseLoc >= 0) {
        double x, y, xc, yc;
        _mousePosition->getValueAtTime(time, x, y);
        _mouseClick->getValueAtTime(time, xc, yc);
        if (!_mousePressed->getValueAtTime(time)) {
            // negative is mouse released
            // see https://github.com/beautypi/shadertoy-iOS-v2/blob/a852d8fd536e0606377a810635c5b654abbee623/shadertoy/ShaderCanvasViewController.m#L315
            xc = -xc;
            yc = -yc;
        }
        glUniform4f (shadertoy->iMouseLoc, x * rs.x, y * rs.y, xc * rs.x, yc * rs.y);
    }
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        if (shadertoy->iChannelLoc[i] >= 0) {
            glActiveTexture (GL_TEXTURE0 + i);
            glBindTexture (GL_TEXTURE_2D, srcIndex[i]);
            glUniform1i (shadertoy->iChannelLoc[i], 0);
        }
    }
    if (shadertoy->iDateLoc >= 0) {
        // do not use the current date, as it may generate a different image at each render
        glUniform4f(shadertoy->iDateLoc, 1970, 1, 1, 0);
    }
    if (shadertoy->iSampleRateLoc >= 0) {
        glUniform1f(shadertoy->iSampleRateLoc, 44100);
    }
    if (shadertoy->ifFragCoordOffsetUniform >= 0) {
        glUniform2f(shadertoy->ifFragCoordOffsetUniform, renderWindow.x1 - dstBoundsFull.x1, renderWindow.y1 - dstBoundsFull.y1);
        DPRINT(("offset=%d,%d\n",(int)(renderWindow.x1 - dstBoundsFull.x1), (int)(renderWindow.y1 - dstBoundsFull.y1)));
    }


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
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        glActiveTexture (GL_TEXTURE0 + i);
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
    for (unsigned i = 0; i < NBINPUTS; ++i) {
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
ShadertoyPlugin::contextAttached()
{
#ifdef DEBUG
    DPRINT(("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER)));
    DPRINT(("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION)));
    DPRINT(("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR)));
    DPRINT(("GL_SHADING_LANGUAGE_VERSION = %s\n", (char *) glGetString(GL_SHADING_LANGUAGE_VERSION)));
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
        if (major == 2 && minor < 1) {
            sendMessage(OFX::Message::eMessageError, "", "Can not render: OpenGL 2.1 or better required for GLSL support.");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    _haveAniso = glutExtensionSupported("GL_EXT_texture_filter_anisotropic");
    if (_haveAniso) {
        GLfloat MaxAnisoMax;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &MaxAnisoMax);
        _maxAnisoMax = MaxAnisoMax;
        DPRINT(("GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT = %f\n", _maxAnisoMax));
    }

#if !defined(USE_OSMESA) && defined(_WINDOWS)
    if (glCreateProgram == NULL) {
      // Program
      glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
      glDeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");
      glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
      glAttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
      glDetachShader = (PFNGLDETACHSHADERPROC)wglGetProcAddress("glDetachShader");
      glLinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
      glGetProgramiv = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
      glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
      glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
      glUniform1i = (PFNGLUNIFORM1IPROC)wglGetProcAddress("glUniform1i");
      glUniform1iv = (PFNGLUNIFORM1IVPROC)wglGetProcAddress("glUniform1iv");
      glUniform2iv = (PFNGLUNIFORM2IVPROC)wglGetProcAddress("glUniform2iv");
      glUniform3iv = (PFNGLUNIFORM3IVPROC)wglGetProcAddress("glUniform3iv");
      glUniform4iv = (PFNGLUNIFORM4IVPROC)wglGetProcAddress("glUniform4iv");
      glUniform1f = (PFNGLUNIFORM1FPROC)wglGetProcAddress("glUniform1f");
      glUniform1fv = (PFNGLUNIFORM1FVPROC)wglGetProcAddress("glUniform1fv");
      glUniform2fv = (PFNGLUNIFORM2FVPROC)wglGetProcAddress("glUniform2fv");
      glUniform3fv = (PFNGLUNIFORM3FVPROC)wglGetProcAddress("glUniform3fv");
      glUniform4fv = (PFNGLUNIFORM4FVPROC)wglGetProcAddress("glUniform4fv");
      glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)wglGetProcAddress("glUniformMatrix4fv");
      glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)wglGetProcAddress("glGetAttribLocation");
      glVertexAttrib1f = (PFNGLVERTEXATTRIB1FPROC)wglGetProcAddress("glVertexAttrib1f");
      glVertexAttrib1fv = (PFNGLVERTEXATTRIB1FVPROC)wglGetProcAddress("glVertexAttrib1fv");
      glVertexAttrib2fv = (PFNGLVERTEXATTRIB2FVPROC)wglGetProcAddress("glVertexAttrib2fv");
      glVertexAttrib3fv = (PFNGLVERTEXATTRIB3FVPROC)wglGetProcAddress("glVertexAttrib3fv");
      glVertexAttrib4fv = (PFNGLVERTEXATTRIB4FVPROC)wglGetProcAddress("glVertexAttrib4fv");
      glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddres s("glEnableVertexAttribArray");
      glBindAttribLocation = (PFNGLBINDATTRIBLOCATIONPROC)wglGetProcAddress("glBindAttribLocation");

      // Shader
      glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
      glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
      glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
      glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
      glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");

      // VBO
      glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
      glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
      glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
      glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");

      // Multitexture
      glActiveTexture = (PFNGLACTIVETEXTUREARBPROC)wglGetProcAddress("glActiveTexture");
      glClientActiveTexture = (PFNGLCLIENTACTIVETEXTUREPROC)wglGetProcAddress("glClientActiveTexture");
      glMultiTexCoord2f = (PFNGLMULTITEXCOORD2FPROC)wglGetProcAddress("glMultiTexCoord2f");

      // Framebuffers
      //glIsFramebuffer = (PFNGLISFRAMEBUFFERPROC)wglGetProcAddress("glIsFramebuffer");
      glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
      glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)wglGetProcAddress("glDeleteFramebuffers");
      glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
      glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");
      //glFramebufferTexture1D = (PFNGLFRAMEBUFFERTEXTURE1DPROC)wglGetProcAddress("glFramebufferTexture1D");
      glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
      //glFramebufferTexture3D = (PFNGLFRAMEBUFFERTEXTURE3DPROC)wglGetProcAddress("glFramebufferTexture3D");
      //glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)wglGetProcAddress("glFramebufferRenderbuffer");
      //glGetFramebufferAttachmentParameteriv = (PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC)wglGetProcAddress("glGetFramebufferAttachmentParameteriv");
    }
#endif

    // Shadertoy:
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
}
