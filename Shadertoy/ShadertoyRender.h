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
#include <cmath>
#include <algorithm>
//#define DEBUG_TIME
#ifdef DEBUG_TIME
#include <sys/time.h>
#endif

#include "ofxsMacros.h"
#include "ofxsMultiThread.h"
#include "ofxsCoords.h"

#ifndef M_LN2
#define M_LN2       0.693147180559945309417232121458176568  /* loge(2)        */
#endif

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
//#  include <OpenGL/glu.h>
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
//#  include <GL/glu.h>
#endif

#define NBINPUTS SHADERTOY_NBINPUTS
#define NBUNIFORMS SHADERTOY_NBUNIFORMS

struct ShadertoyShader
{
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
        , ifFragCoordOffsetUniformLoc(-1)
        , iRenderScaleLoc(-1)
    {
        std::fill(iChannelLoc, iChannelLoc + NBINPUTS, -1);
        std::fill(iParamLoc, iParamLoc + NBUNIFORMS, -1);
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
    GLint ifFragCoordOffsetUniformLoc;
    GLint iRenderScaleLoc;
    GLint iParamLoc[NBUNIFORMS];
    GLint iChannelLoc[NBINPUTS];
};

#if !defined(USE_OSMESA) && ( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) )
// Program
#ifdef GL_VERSION_2_0
static PFNGLCREATEPROGRAMPROC glCreateProgram = NULL;
static PFNGLDELETEPROGRAMPROC glDeleteProgram = NULL;
static PFNGLUSEPROGRAMPROC glUseProgram = NULL;
static PFNGLATTACHSHADERPROC glAttachShader = NULL;
static PFNGLDETACHSHADERPROC glDetachShader = NULL;
static PFNGLLINKPROGRAMPROC glLinkProgram = NULL;
static PFNGLGETPROGRAMIVPROC glGetProgramiv = NULL;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = NULL;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = NULL;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
static PFNGLUNIFORM1IPROC glUniform1i = NULL;
static PFNGLUNIFORM2IPROC glUniform2i = NULL;
static PFNGLUNIFORM3IPROC glUniform3i = NULL;
static PFNGLUNIFORM4IPROC glUniform4i = NULL;
static PFNGLUNIFORM1IVPROC glUniform1iv = NULL;
static PFNGLUNIFORM2IVPROC glUniform2iv = NULL;
static PFNGLUNIFORM3IVPROC glUniform3iv = NULL;
static PFNGLUNIFORM4IVPROC glUniform4iv = NULL;
static PFNGLUNIFORM1FPROC glUniform1f = NULL;
static PFNGLUNIFORM2FPROC glUniform2f = NULL;
static PFNGLUNIFORM3FPROC glUniform3f = NULL;
static PFNGLUNIFORM4FPROC glUniform4f = NULL;
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
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = NULL;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = NULL;
static PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = NULL;
static PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation = NULL;
static PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform = NULL;
#endif

// Shader
#ifdef GL_VERSION_2_0
static PFNGLCREATESHADERPROC glCreateShader = NULL;
static PFNGLDELETESHADERPROC glDeleteShader = NULL;
static PFNGLSHADERSOURCEPROC glShaderSource = NULL;
static PFNGLCOMPILESHADERPROC glCompileShader = NULL;
static PFNGLGETSHADERIVPROC glGetShaderiv = NULL;
#endif

// VBO
#ifdef GL_VERSION_1_5
static PFNGLGENBUFFERSPROC glGenBuffers = NULL;
static PFNGLBINDBUFFERPROC glBindBuffer = NULL;
static PFNGLBUFFERDATAPROC glBufferData = NULL;
#endif

//Multitexture
#ifdef GL_VERSION_1_3
static PFNGLACTIVETEXTUREARBPROC glActiveTexture = NULL;
#endif
#ifdef GL_VERSION_1_3_DEPRECATED
//static PFNGLCLIENTACTIVETEXTUREPROC glClientActiveTexture = NULL;
//static PFNGLMULTITEXCOORD2FPROC glMultiTexCoord2f = NULL;
#endif

#ifdef GL_ARB_framebuffer_object
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

#ifdef GL_ARB_sync
// Sync Objects https://www.opengl.org/wiki/Sync_Object
//typedef GLsync (APIENTRYP PFNGLFENCESYNCPROC) (GLenum condition, GLbitfield flags);
static PFNGLFENCESYNCPROC glFenceSync = NULL;
//typedef GLboolean (APIENTRYP PFNGLISSYNCPROC) (GLsync sync);
static PFNGLISSYNCPROC glIsSync = NULL;
//typedef void (APIENTRYP PFNGLDELETESYNCPROC) (GLsync sync);
static PFNGLDELETESYNCPROC glDeleteSync = NULL;
//typedef GLenum (APIENTRYP PFNGLCLIENTWAITSYNCPROC) (GLsync sync, GLbitfield flags, GLuint64 timeout);
static PFNGLCLIENTWAITSYNCPROC glClientWaitSync = NULL;
//typedef void (APIENTRYP PFNGLWAITSYNCPROC) (GLsync sync, GLbitfield flags, GLuint64 timeout);
static PFNGLWAITSYNCPROC glWaitSync = NULL;
#endif

#endif // if !defined(USE_OSMESA) && ( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) )

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
inline void
glError() {}

inline const char*
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
            std::cout << "GL_ERROR :" << __FILE__ << " " << __LINE__ << " " << glErrorString(_glerror_) << std::endl; \
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
    std::fflush(stderr);
#ifdef _WIN32
    OutputDebugString(msg);
#endif
    va_end(ap);
}

#endif // ifndef DEBUG


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
        , _openGLContextData()
    {
        assert(_openGLContextData.imageShader == NULL);
        _openGLContextData.imageShader = new ShadertoyShader;
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
        delete (ShadertoyShader*)_openGLContextData.imageShader;
        _openGLContextData.imageShader = NULL;
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

    ShadertoyPlugin *_effect;
    // information about the current Mesa context
    OSMesaContext _ctx;
    GLenum _ctxFormat;
    GLint _ctxDepthBits;
    GLint _ctxStencilBits;
    GLint _ctxAccumBits;

    OpenGLContextData _openGLContextData; // context-specific data
};

void
ShadertoyPlugin::initMesa()
{
}

void
ShadertoyPlugin::exitMesa()
{
    AutoMutex lock( _osmesaMutex.get() );

    for (std::list<OSMesaPrivate *>::iterator it = _osmesa.begin(); it != _osmesa.end(); ++it) {
        delete *it;
    }
    _osmesa.clear();
}

#endif // USE_OSMESA


#ifdef USE_OPENGL

void
ShadertoyPlugin::initOpenGL()
{
    assert(_openGLContextData.imageShader == NULL);
    _openGLContextData.imageShader = new ShadertoyShader;
}

void
ShadertoyPlugin::exitOpenGL()
{
    delete ((ShadertoyShader*)_openGLContextData.imageShader);
    _openGLContextData.imageShader = NULL;
}

#endif // USE_OPENGL

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

static
GLuint
compileShader(GLenum shaderType,
              const char *shader,
              std::string &errstr)
{
    GLuint s = glCreateShader(shaderType);

    if (s == 0) {
        DPRINT( ("Failed to create shader from\n====\n%s\n===\n", shader) );

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
            errstr += "\nError log:\n";
            errstr += infoLog;
            delete [] infoLog;
        } else {
            errstr += "(no error log)";
        }

        glDeleteShader(s);
        DPRINT( ( "%s\n", errstr.c_str() ) );

        return 0;
    }

    return s;
} // compileShader

static
GLuint
compileAndLinkProgram(const char *vertexShader,
                      const char *fragmentShader,
                      std::string &errstr)
{
    DPRINT( ("CompileAndLink\n") );
    GLuint program = glCreateProgram();
    if (program == 0) {
        DPRINT( ("Failed to create program\n") );
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
            DPRINT( ( "%s\n", errstr.c_str() ) );

            return 0;
        }
    } else {
        glDeleteProgram(program);
    }

    if (vs) {
        glDeleteShader(vs);
    }

    if (fs) {
        glDeleteShader(fs);
    }

    return program;
} // compileAndLinkProgram

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
// improve OpenGL ES 2.0 portability,
// see https://en.wikibooks.org/wiki/OpenGL_Programming/Modern_OpenGL_Tutorial_03#OpenGL_ES_2_portability
static std::string fsHeader =
#ifdef GL_ES_VERSION_2_0
    "#version 100\n" // OpenGL ES 2.0
#else
    "#version 120\n" // OpenGL 2.1
#endif
#ifdef GL_ES_VERSION_2_0
    "#extension GL_EXT_shader_texture_lod : enable\n"
    "#extension GL_OES_standard_derivatives : enable\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "#else\n"
    "precision mediump float;\n"
    "precision mediump int;\n"
    "#endif\n"
    "precision mediump sampler2D;\n"
#else
// Ignore GLES 2 precision specifiers:
    "#define lowp   \n"
    "#define mediump\n"
    "#define highp  \n"
#endif
    "uniform vec3      iResolution;\n"
    "uniform float     iGlobalTime;\n"
    "uniform float     iTimeDelta;\n"
    "uniform int       iFrame;\n"
    "uniform float     iChannelTime["STRINGISE (NBINPUTS)"];\n"
    "uniform vec3      iChannelResolution["STRINGISE (NBINPUTS)"];\n"
    "uniform vec4      iMouse;\n"
    "uniform vec4      iDate;\n"
    "uniform float     iSampleRate;\n"
    "uniform vec2      ifFragCoordOffsetUniform;\n"
    "uniform vec2      iRenderScale;\n" // the OpenFX renderscale
;

// https://raw.githubusercontent.com/beautypi/shadertoy-iOS-v2/master/shadertoy/shaders/fragment_main_image.glsl
static std::string fsFooter =
    "void main(void)\n"
    "{\n"
    "  mainImage(gl_FragColor, gl_FragCoord.xy + ifFragCoordOffsetUniform );\n"
    "}\n";
void
ShadertoyPlugin::RENDERFUNC(const OFX::RenderArguments &args)
{
    const double time = args.time;
    bool mipmap = true;
    bool anisotropic = true;
#ifdef DEBUG_TIME
    struct timeval t1, t2;
    gettimeofday(&t1, NULL);
#endif

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
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);

        return;
    }
#  endif
# endif

    const OfxRectI& renderWindow = args.renderWindow;
    //DPRINT( ("Render: window = [%d, %d - %d, %d]\n", renderWindow.x1, renderWindow.y1, renderWindow.x2, renderWindow.y2) );


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
    DPRINT( ( "openGL: output texture index %d, target %d, depth %s\n",
              dstIndex, dstTarget, mapBitDepthEnumToStr(dstBitDepth) ) );
# endif

# ifdef USE_OPENGL
    std::auto_ptr<const OFX::Texture> src[NBINPUTS];
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        src[i].reset( ( _srcClips[i] && _srcClips[i]->isConnected() ) ?
                      _srcClips[i]->loadTexture(time) : 0 );
    }
# else
    std::auto_ptr<const OFX::Image> src[NBINPUTS];
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        src[i].reset( ( _srcClips[i] && _srcClips[i]->isConnected() ) ?
                      _srcClips[i]->fetchImage(time) : 0 );
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
        if ( src[i].get() ) {
            srcBitDepth[i] = src[i]->getPixelDepth();
            srcComponents[i] = src[i]->getPixelComponents();
            if ( (srcBitDepth[i] != dstBitDepth) || (srcComponents[i] != dstComponents) ) {
                OFX::throwSuiteStatusException(kOfxStatErrImageFormat);

                return;
            }
# ifdef USE_OPENGL
            srcIndex[i] = (GLuint)src[i]->getIndex();
            srcTarget[i] = (GLenum)src[i]->getTarget();
            DPRINT( ( "openGL: source texture %u index %d, target %d, depth %s\n",
                      i, srcIndex[i], srcTarget[i], mapBitDepthEnumToStr(srcBitDepth[i]) ) );
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
                if ( (format != formati) ||
                     ( depthBits != depthBitsi) ||
                     ( stencilBits != stencilBitsi) ||
                     ( accumBits != accumBitsi) ||
                     ( type != typei) ) {
                    // all inputs should have the same format
                    OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
                }
            }
#endif // ifdef USE_OSMESA
        }
    }

#ifdef USE_OSMESA
    const OfxRectI dstBounds = dst->getBounds();
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
    if (args.openGLContextData) {
        // host provided kNatronOfxImageEffectPropOpenGLContextData,
        // which was returned by kOfxActionOpenGLContextAttached
        contextData = (OpenGLContextData*)args.openGLContextData;
    } else if (!_openGLContextAttached) {
        // Sony Catalyst Edit never calls kOfxActionOpenGLContextAttached
        contextAttached(false);
        _openGLContextAttached = true;
    }
#endif
#ifdef USE_OSMESA
    OpenGLContextData* contextData = &osmesa->_openGLContextData;
#endif

    // compile and link the shader if necessary
    ShadertoyShader *shadertoy;
    {
        AutoMutex lock( _shaderMutex.get() );
        bool must_recompile = false;
        bool uniforms_changed = false;
        shadertoy = (ShadertoyShader *)contextData->imageShader;
        assert(shadertoy);
        must_recompile = (_imageShaderID != contextData->imageShaderID);
        contextData->imageShaderID = _imageShaderID;
        uniforms_changed = (_imageShaderUniformsID != contextData->imageShaderUniformsID);
        contextData->imageShaderUniformsID = _imageShaderUniformsID;

        if (must_recompile) {
            if (shadertoy->program) {
                glDeleteProgram(shadertoy->program);
                shadertoy->program = 0;
            }
            std::string str;
            _imageShaderSource->getValue(str);
            std::string fsSource = fsHeader;
            for (unsigned i = 0; i < NBINPUTS; ++i) {
                fsSource += std::string("uniform sampler2D iChannel") + (char)('0' + i) + ";\n";
            }
            fsSource += "#line 1\n";
            fsSource += str + '\n' + fsFooter;
            std::string errstr;
            shadertoy->program = compileAndLinkProgram(vsSource.c_str(), fsSource.c_str(), errstr);
            if (shadertoy->program == 0) {
                setPersistentMessage(OFX::Message::eMessageError, "", "Failed to compile and link program");
                sendMessage( OFX::Message::eMessageError, "", errstr.c_str() );
                OFX::throwSuiteStatusException(kOfxStatFailed);

                return;
            }
            shadertoy->iResolutionLoc        = glGetUniformLocation(shadertoy->program, "iResolution");
            shadertoy->iGlobalTimeLoc        = glGetUniformLocation(shadertoy->program, "iGlobalTime");
            shadertoy->iTimeDeltaLoc         = glGetUniformLocation(shadertoy->program, "iTimeDelta");
            shadertoy->iFrameLoc             = glGetUniformLocation(shadertoy->program, "iFrame");
            shadertoy->iChannelTimeLoc       = glGetUniformLocation(shadertoy->program, "iChannelTime");
            shadertoy->iMouseLoc             = glGetUniformLocation(shadertoy->program, "iMouse");
            shadertoy->iDateLoc              = glGetUniformLocation(shadertoy->program, "iDate");
            shadertoy->iSampleRateLoc        = glGetUniformLocation(shadertoy->program, "iSampleRate");
            shadertoy->iChannelResolutionLoc = glGetUniformLocation(shadertoy->program, "iChannelResolution");
            shadertoy->ifFragCoordOffsetUniformLoc = glGetUniformLocation(shadertoy->program, "ifFragCoordOffsetUniform");
            shadertoy->iRenderScaleLoc = glGetUniformLocation(shadertoy->program, "iRenderScale");
            char iChannelX[10] = "iChannelX"; // index 8 holds the channel character
            assert(NBINPUTS < 10 && iChannelX[8] == 'X');
            for (unsigned i = 0; i < NBINPUTS; ++i) {
                iChannelX[8] = '0' + i;
                shadertoy->iChannelLoc[i] = glGetUniformLocation(shadertoy->program, iChannelX);
                //printf("%s -> %d\n", iChannelX, (int)shadertoy->iChannelLoc[i]);
            }
        }
        if (must_recompile || uniforms_changed) {
            std::fill(shadertoy->iParamLoc, shadertoy->iParamLoc + NBUNIFORMS, -1);
            unsigned paramCount = std::max( 0, std::min(_paramCount->getValue(), NBUNIFORMS) );
            for (unsigned i = 0; i < paramCount; ++i) {
                std::string paramName;
                _paramName[i]->getValue(paramName);
                if ( !paramName.empty() ) {
                    shadertoy->iParamLoc[i] = glGetUniformLocation( shadertoy->program, paramName.c_str() );
                }
            }
        }
    }


#ifdef USE_OSMESA
    // load the source image into a texture
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Non-power-of-two textures are supported if the GL version is 2.0 or greater, or if the implementation exports the GL_ARB_texture_non_power_of_two extension. (Mesa does, of course)

    std::vector<GLenum> srcTarget(4, GL_TEXTURE_2D);
    std::vector<GLuint> srcIndex(NBINPUTS);
    glActiveTexture(GL_TEXTURE0);
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        if ( src[i].get() && (shadertoy->iChannelLoc[i] >= 0) ) {
            glGenTextures(1, &srcIndex[i]);
            OfxRectI srcBounds = src[i]->getBounds();
            glBindTexture(srcTarget[i], srcIndex[i]);
            if (mipmap) {
                // this must be done before glTexImage2D
                glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
                // requires extension GL_SGIS_generate_mipmap or OpenGL 1.4.
                glTexParameteri(srcTarget[i], GL_GENERATE_MIPMAP, GL_TRUE); // Allocate the mipmaps
            }

            glTexImage2D( srcTarget[i], 0, format,
                          srcBounds.x2 - srcBounds.x1, srcBounds.y2 - srcBounds.y1, 0,
                          format, type, src[i]->getPixelData() );
            glBindTexture(srcTarget[i], 0);
        }
    }
#endif

    int w = (renderWindow.x2 - renderWindow.x1);
    int h = (renderWindow.y2 - renderWindow.y1);

    // setup the projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClear(GL_DEPTH_BUFFER_BIT); // does not hurt, even if there is no Z-buffer (Sony Catalyst)

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
        GLfloat rv[3 * NBINPUTS];
        for (unsigned i = 0; i < NBINPUTS; ++i) {
        }
        glUniform3fv(shadertoy->iChannelResolutionLoc, NBINPUTS, rv);
    }
    if (shadertoy->iMouseLoc >= 0) {
        double x, y, xc, yc;
        _mousePosition->getValueAtTime(time, x, y);
        _mouseClick->getValueAtTime(time, xc, yc);
        if ( !_mousePressed->getValueAtTime(time) ) {
            // negative is mouse released
            // see https://github.com/beautypi/shadertoy-iOS-v2/blob/a852d8fd536e0606377a810635c5b654abbee623/shadertoy/ShaderCanvasViewController.m#L315
            xc = -xc;
            yc = -yc;
        }
        glUniform4f (shadertoy->iMouseLoc, x * rs.x, y * rs.y, xc * rs.x, yc * rs.y);
    }
    unsigned paramCount = std::max( 0, std::min(_paramCount->getValue(), NBUNIFORMS) );
    for (unsigned i = 0; i < paramCount; ++i) {
        if (shadertoy->iParamLoc[i] >= 0) {
            UniformTypeEnum paramType = (UniformTypeEnum)_paramType[i]->getValue();
            switch (paramType) {
            case eUniformTypeNone: {
                break;
            }
            case eUniformTypeBool: {
                bool v = _paramValueBool[i]->getValue();
                glUniform1i(shadertoy->iParamLoc[i], v);
                break;
            }
            case eUniformTypeInt: {
                int v = _paramValueInt[i]->getValue();
                glUniform1i(shadertoy->iParamLoc[i], v);
                break;
            }
            case eUniformTypeFloat: {
                double v = _paramValueFloat[i]->getValue();
                glUniform1f(shadertoy->iParamLoc[i], v);
                break;
            }
            case eUniformTypeVec2: {
                double x, y;
                _paramValueVec2[i]->getValue(x, y);
                glUniform2f(shadertoy->iParamLoc[i], x, y);
                break;
            }
            case eUniformTypeVec3: {
                double x, y, z;
                _paramValueVec3[i]->getValue(x, y, z);
                glUniform3f(shadertoy->iParamLoc[i], x, y, z);
                break;
            }
            case eUniformTypeVec4: {
                double x, y, z, w;
                _paramValueVec4[i]->getValue(x, y, z, w);
                glUniform4f(shadertoy->iParamLoc[i], x, y, z, w);
                break;
            }
            default: {
                assert(false);
                break;
            }
            }
        }
    }
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        if ( src[i].get() && (shadertoy->iChannelLoc[i] >= 0) ) {
            glEnable(srcTarget[i]);
            glUniform1i(shadertoy->iChannelLoc[i], i);
            glBindTexture(srcTarget[i], srcIndex[i]);
        } else {
            glBindTexture(srcTarget[i], 0);
        }
    }
    if (shadertoy->iDateLoc >= 0) {
        // do not use the current date, as it may generate a different image at each render
        glUniform4f(shadertoy->iDateLoc, 1970, 1, 1, 0);
    }
    if (shadertoy->iSampleRateLoc >= 0) {
        glUniform1f(shadertoy->iSampleRateLoc, 44100);
    }
    if (shadertoy->ifFragCoordOffsetUniformLoc >= 0) {
        glUniform2f(shadertoy->ifFragCoordOffsetUniformLoc, renderWindow.x1 - dstBoundsFull.x1, renderWindow.y1 - dstBoundsFull.y1);
        //DPRINT(("offset=%d,%d\n",(int)(renderWindow.x1 - dstBoundsFull.x1), (int)(renderWindow.y1 - dstBoundsFull.y1)));
    }
    if (shadertoy->iRenderScaleLoc >= 0) {
        glUniform2f(shadertoy->iRenderScaleLoc, rs.x, rs.y);
    }

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Are your images pretty large? Maybe one approach would to render the scene in tiled chunks.
    // For example, divide the window into an NxM grid of tiles, then render the scene into each tile with glScissor.
    // After rendering each tile, check if there's user input and abort drawing the grid if needed.
    // Ideally the tile size should be a multiple of llvmpipe's tile size which is 64x64.
    // llvmpipe employs multiple threads to process tiles in parallel so your tiles should probably
    // be 128x128 for 4 cores, 256x128 for 8 cores, etc.
    int tile_w = w;
    int tile_h = h;
#ifdef USE_OSMESA
    glEnable(GL_SCISSOR_TEST);
    {
        int nCPUs = OFX::MultiThread::getNumCPUs();
        // - take the square root of nCPUs
        // - compute the next closest power of two -> this gives the number of tiles for the x dimension
        int pow2_x = std::ceil(std::log(std::sqrt(nCPUs)) / M_LN2);
        tile_w = 64 * (1 << pow2_x);
        // - compute the next power of two for the other side
        int pow2_y = std::ceil(std::log( nCPUs / (double)(1 << pow2_x) ) / M_LN2);
        tile_h = 64 * (1 << pow2_y);
        DPRINT( ("Shadertoy: tile size: %d %d for %d CPUs\n", tile_w, tile_h, nCPUs) );
    }
#endif

    bool aborted = abort();
    for (int y1 = 0; y1 < h && !aborted; y1 += tile_h) {
        for (int x1 = 0; x1 < w && !aborted; x1 += tile_w) {
#ifdef DEBUG_TIME
            struct timeval t1, t2;
            gettimeofday(&t1, NULL);
#endif
            glScissor(x1, y1, tile_w, tile_h);
            glBegin(GL_QUADS);
            glVertex2f(0, 0);
            glVertex2f(0, h);
            glVertex2f(w, h);
            glVertex2f(w, 0);
            glEnd();
            aborted = abort();
#ifdef USE_OSMESA
            // render the tile if we are using osmesa
            if (!aborted) {
                glFlush(); // waits until commands are submitted but does not wait for the commands to finish executing
            }
#endif
#ifdef DEBUG_TIME
            gettimeofday(&t2, NULL);
            DPRINT( ("rendering tile: %d %d %d %d took %d us\n", x1, y1, tile_w, tile_h, 1000000*(t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec)) );
#endif
        }
    }
    if (aborted) {
        DPRINT( ("Shadertoy: aborted!\n") );
    }

    for (unsigned i = 0; i < NBINPUTS; ++i) {
        if (shadertoy->iChannelLoc[i] >= 0) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    glUseProgram(0);

    // done; clean up.
    glPopAttrib();

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
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        if ( src[i].get() ) {
            glDeleteTextures(1, &srcIndex[i]);
        }
    }

#if 0//ifdef GL_ARB_sync
     // glFenceSync does not seem to work properly in OSMesa:
     // we get random black images.
    if (!glFenceSync) {
        // If Sync Objects not available (but they should always be there in Mesa)
        if ( !abort() ) {
            glFlush(); // waits until commands are submitted but does not wait for the commands to finish executing
            glFinish(); // waits for all previously submitted commands to complete executing
        }
    } else if ( !abort() ) {
        glCheckError();
        GLsync fenceId = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
        if ( !fenceId && !abort() ) {
            // glFenceSync failed for some reason
            glCheckError();
            glFlush(); // waits until commands are submitted but does not wait for the commands to finish executing
            glFinish(); // waits for all previously submitted commands to complete executing
        } else {
            while ( !abort() ) {
                GLenum result = glClientWaitSync( fenceId, GL_SYNC_FLUSH_COMMANDS_BIT, GLuint64(10 * 1000) ); // 10ms timeout
                if (result != GL_TIMEOUT_EXPIRED) {
                    break; // we ignore timeouts and wait until all OpenGL commands are processed!
                }
            }
            glCheckError();
        }
    }
#else
    if ( !aborted ) {
        glFlush(); // waits until commands are submitted but does not wait for the commands to finish executing
        glFinish(); // waits for all previously submitted commands to complete executing
    }
#endif
    // make sure the buffer is not referenced anymore
    osmesa->setContext(format, depthBits, type, stencilBits, accumBits, NULL, dstBounds);
    OSMesaMakeCurrent(NULL, NULL, 0, 0, 0); // disactivate the context so that it can be used from another thread
    assert(OSMesaGetCurrentContext() == NULL);

    // We're finished with this osmesa, make it available for other renders
    {
        AutoMutex lock( _osmesaMutex.get() );
        _osmesa.push_back(osmesa);
    }
#endif // ifdef USE_OSMESA
#ifdef DEBUG_TIME
    gettimeofday(&t2, NULL);
    DPRINT( ("rendering took %d us\n", 1000000*(t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec)) );
#endif

} // ShadertoyPlugin::RENDERFUNC

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
 * Action called when an effect has just been attached to an OpenGL
 * context.
 *
 * The purpose of this action is to allow a plugin to set up any data it may need
 * to do OpenGL rendering in an instance. For example...
 *  - allocate a lookup table on a GPU,
 *  - create an openCL or CUDA context that is bound to the host's OpenGL
 *    context so it can share buffers.
 */
void*
ShadertoyPlugin::contextAttached(bool createContextData)
{
#ifdef DEBUG
    DPRINT( ( "GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER) ) );
    DPRINT( ( "GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION) ) );
    DPRINT( ( "GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR) ) );
    DPRINT( ( "GL_SHADING_LANGUAGE_VERSION = %s\n", (char *) glGetString(GL_SHADING_LANGUAGE_VERSION) ) );
    DPRINT( ( "GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS) ) );
#endif
    {
        AutoMutex lock( _rendererInfoMutex.get() );
#ifdef USE_OSMESA
        std::string &message = _rendererInfoMesa;
#else
        std::string &message = _rendererInfoGL;
#endif
        if ( message.empty() ) {
            message += "OpenGL renderer information:";
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
    if (major < 3) {
        if ( (major == 2) && (minor < 1) ) {
            sendMessage(OFX::Message::eMessageError, "", "Can not render: OpenGL 2.1 or better required for GLSL support.");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    OpenGLContextData* contextData = &_openGLContextData;
#ifdef USE_OPENGL
    assert(_openGLContextData->imageShader);
    if (createContextData) {
        contextData = new OpenGLContextData;
        contextData->imageShader = new ShadertoyShader;
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
    if (glCreateProgram == NULL) {
        // Program
#ifdef GL_VERSION_2_0
        glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
        glDeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");
        glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
        glAttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
        glDetachShader = (PFNGLDETACHSHADERPROC)wglGetProcAddress("glDetachShader");
        glLinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
        glGetProgramiv = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
        glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)wglGetProcAddress("glGetProgramInfoLog");
        glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
        glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
        glUniform1i = (PFNGLUNIFORM1IPROC)wglGetProcAddress("glUniform1i");
        glUniform2i = (PFNGLUNIFORM2IPROC)wglGetProcAddress("glUniform2i");
        glUniform3i = (PFNGLUNIFORM3IPROC)wglGetProcAddress("glUniform3i");
        glUniform4i = (PFNGLUNIFORM4IPROC)wglGetProcAddress("glUniform4i");
        glUniform1iv = (PFNGLUNIFORM1IVPROC)wglGetProcAddress("glUniform1iv");
        glUniform2iv = (PFNGLUNIFORM2IVPROC)wglGetProcAddress("glUniform2iv");
        glUniform3iv = (PFNGLUNIFORM3IVPROC)wglGetProcAddress("glUniform3iv");
        glUniform4iv = (PFNGLUNIFORM4IVPROC)wglGetProcAddress("glUniform4iv");
        glUniform1f = (PFNGLUNIFORM1FPROC)wglGetProcAddress("glUniform1f");
        glUniform2f = (PFNGLUNIFORM2FPROC)wglGetProcAddress("glUniform2f");
        glUniform3f = (PFNGLUNIFORM3FPROC)wglGetProcAddress("glUniform3f");
        glUniform4f = (PFNGLUNIFORM4FPROC)wglGetProcAddress("glUniform4f");
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
        glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
        glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
        glBindAttribLocation = (PFNGLBINDATTRIBLOCATIONPROC)wglGetProcAddress("glBindAttribLocation");
#endif

        // Shader
#ifdef GL_VERSION_2_0
        glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
        glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
        glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
        glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
        glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
#endif

        // VBO
#ifdef GL_VERSION_1_5
        glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
        glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
        glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
#endif

        // Multitexture
#ifdef GL_VERSION_1_3
        glActiveTexture = (PFNGLACTIVETEXTUREARBPROC)wglGetProcAddress("glActiveTexture");
#endif
#ifdef GL_VERSION_1_3_DEPRECATED
        //glClientActiveTexture = (PFNGLCLIENTACTIVETEXTUREPROC)wglGetProcAddress("glClientActiveTexture");
        //glMultiTexCoord2f = (PFNGLMULTITEXCOORD2FPROC)wglGetProcAddress("glMultiTexCoord2f");
#endif

        // Framebuffers
#ifdef GL_ARB_framebuffer_object
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
#endif

#ifdef GL_ARB_sync
        // Sync Objects https://www.opengl.org/wiki/Sync_Object
        glFenceSync = (PFNGLFENCESYNCPROC)wglGetProcAddress("glFenceSync​");
        glIsSync = (PFNGLISSYNCPROC)wglGetProcAddress("glIsSync");
        glDeleteSync = (PFNGLDELETESYNCPROC)wglGetProcAddress("glDeleteSync");
        glClientWaitSync = (PFNGLCLIENTWAITSYNCPROC)wglGetProcAddress("glClientWaitSync​");
        glWaitSync = (PFNGLWAITSYNCPROC)wglGetProcAddress("glWaitSync​");
#endif
    }
#endif // if !defined(USE_OSMESA) && ( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) )

    contextData->imageShader = NULL;
    contextData->imageShaderID = 0;
    contextData->imageShaderUniformsID = 0;

#ifdef USE_OPENGL
    if (createContextData) {
        return contextData;
    }
#endif
    return NULL;
} // ShadertoyPlugin::contextAttached

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
ShadertoyPlugin::contextDetached(void* contextData)
{
    // Shadertoy:
#ifdef USE_OPENGL
    if (contextData) {
        delete (ShadertoyShader*)((OpenGLContextData*)contextData)->imageShader;
        ((OpenGLContextData*)contextData)->imageShader = NULL;
        delete (OpenGLContextData*)contextData;
    } else {
        _openGLContextAttached = false;
    }
#else
    assert(!contextData); // context data is handled differently in CPU rendering
#endif
}

