/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2018 INRIA
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
#include "ofxsThreadSuite.h"
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

#include "ofxsOGLDebug.h"

#ifndef DEBUG
#define DPRINT(args) (void)0
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
    OutputDebugString(str);
#endif
    va_end(ap);
}

#endif // ifndef DEBUG

#define NBINPUTS SHADERTOY_NBINPUTS
#define NBUNIFORMS SHADERTOY_NBUNIFORMS

struct ShadertoyShader
{
    ShadertoyShader()
        : program(0)
        , iResolutionLoc(-1)
        , iTimeLoc(-1)
        , iTimeDeltaLoc(-1)
        , iFrameLoc(-1)
        , iChannelTimeLoc(-1)
        , iMouseLoc(-1)
        , iDateLoc(-1)
        , iSampleRateLoc(-1)
        , iChannelResolutionLoc(-1)
        , ifFragCoordOffsetUniformLoc(-1)
        , iRenderScaleLoc(-1)
        , iChannelOffsetLoc(-1)
    {
        std::fill(iChannelLoc, iChannelLoc + NBINPUTS, -1);
        std::fill(iParamLoc, iParamLoc + NBUNIFORMS, -1);
    }

    GLuint program;
    GLint iResolutionLoc;
    GLint iTimeLoc;
    GLint iTimeDeltaLoc;
    GLint iFrameLoc;
    GLint iChannelTimeLoc;
    GLint iMouseLoc;
    GLint iDateLoc;
    GLint iSampleRateLoc;
    GLint iChannelResolutionLoc;
    GLint ifFragCoordOffsetUniformLoc;
    GLint iRenderScaleLoc;
    GLint iChannelOffsetLoc;
    GLint iParamLoc[NBUNIFORMS];
    GLint iChannelLoc[NBINPUTS];
};

#if !defined(USE_OSMESA) && ( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) )
// for i in `fgrep "static PFN" ShadertoyRender.h |sed -e s@//@@ |awk '{print $2}'|fgrep -v i`; do fgrep $i /opt/osmesa/include/GL/glext.h ; done

// Program
#ifndef GL_VERSION_1_4
#define GL_MIRRORED_REPEAT                0x8370
#endif

#ifndef GL_VERSION_2_0
typedef GLuint (APIENTRYP PFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRYP PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRYP PFNGLDETACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef GLint (APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void (APIENTRYP PFNGLGETUNIFORMFVPROC)(GLuint program, GLint location, GLfloat *params);
typedef void (APIENTRYP PFNGLGETUNIFORMIVPROC)(GLuint program, GLint location, GLint *params);
typedef void (APIENTRYP PFNGLGETVERTEXATTRIBDVPROC)(GLuint index, GLenum pname, GLdouble *params);
typedef void (APIENTRYP PFNGLGETVERTEXATTRIBFVPROC)(GLuint index, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNGLGETVERTEXATTRIBIVPROC)(GLuint index, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNGLGETVERTEXATTRIBPOINTERVPROC)(GLuint index, GLenum pname, void **pointer);
typedef GLboolean (APIENTRYP PFNGLISPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void (APIENTRYP PFNGLUNIFORM2IPROC)(GLint location, GLint v0, GLint v1);
typedef void (APIENTRYP PFNGLUNIFORM3IPROC)(GLint location, GLint v0, GLint v1, GLint v2);
typedef void (APIENTRYP PFNGLUNIFORM4IPROC)(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
typedef void (APIENTRYP PFNGLUNIFORM1IVPROC)(GLint location, GLsizei count, const GLint *value);
typedef void (APIENTRYP PFNGLUNIFORM2IVPROC)(GLint location, GLsizei count, const GLint *value);
typedef void (APIENTRYP PFNGLUNIFORM3IVPROC)(GLint location, GLsizei count, const GLint *value);
typedef void (APIENTRYP PFNGLUNIFORM4IVPROC)(GLint location, GLsizei count, const GLint *value);
typedef void (APIENTRYP PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void (APIENTRYP PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void (APIENTRYP PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (APIENTRYP PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (APIENTRYP PFNGLUNIFORM1FVPROC)(GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNGLUNIFORM2FVPROC)(GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNGLUNIFORM3FVPROC)(GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNGLUNIFORM4FVPROC)(GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef GLint (APIENTRYP PFNGLGETATTRIBLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void (APIENTRYP PFNGLVERTEXATTRIB1FPROC)(GLuint index, GLfloat x);
typedef void (APIENTRYP PFNGLVERTEXATTRIB1FVPROC)(GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNGLVERTEXATTRIB2FVPROC)(GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNGLVERTEXATTRIB3FVPROC)(GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNGLVERTEXATTRIB4FVPROC)(GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
typedef void (APIENTRYP PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (APIENTRYP PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (APIENTRYP PFNGLGETACTIVEATTRIBPROC)(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
typedef void (APIENTRYP PFNGLBINDATTRIBLOCATIONPROC)(GLuint program, GLuint index, const GLchar *name);
typedef void (APIENTRYP PFNGLGETACTIVEUNIFORMPROC)(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
#endif
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
static PFNGLGETUNIFORMFVPROC glGetUniformfv = NULL;
static PFNGLGETUNIFORMIVPROC glGetUniformiv = NULL;
//static PFNGLGETVERTEXATTRIBDVPROC glGetVertexAttribdv = NULL;
//static PFNGLGETVERTEXATTRIBFVPROC glGetVertexAttribfv = NULL;
//static PFNGLGETVERTEXATTRIBIVPROC glGetVertexAttribiv = NULL;
//static PFNGLGETVERTEXATTRIBPOINTERVPROC glGetVertexAttribPointerv = NULL;
//static PFNGLISPROGRAMPROC glIsProgram = NULL;
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
static PFNGLGETACTIVEATTRIBPROC glGetActiveAttrib = NULL;
static PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation = NULL;
static PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform = NULL;

// Shader
#ifndef GL_VERSION_2_0
typedef GLuint (APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRYP PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
typedef void (APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (APIENTRYP PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef GLboolean (APIENTRYP PFNGLISSHADERPROC)(GLuint shader);
#endif
static PFNGLCREATESHADERPROC glCreateShader = NULL;
static PFNGLDELETESHADERPROC glDeleteShader = NULL;
static PFNGLSHADERSOURCEPROC glShaderSource = NULL;
static PFNGLCOMPILESHADERPROC glCompileShader = NULL;
static PFNGLGETSHADERIVPROC glGetShaderiv = NULL;
//static PFNGLISSHADERPROC glIsShader = NULL;

// VBO
#ifndef GL_VERSION_1_5
typedef void (APIENTRYP PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void (APIENTRYP PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (APIENTRYP PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
#endif
static PFNGLGENBUFFERSPROC glGenBuffers = NULL;
static PFNGLBINDBUFFERPROC glBindBuffer = NULL;
static PFNGLBUFFERDATAPROC glBufferData = NULL;

//Multitexture
#ifndef GL_VERSION_1_3
typedef void (APIENTRYP PFNGLACTIVETEXTUREARBPROC)(GLenum texture);
#endif
#ifndef GL_VERSION_1_3_DEPRECATED
typedef void (APIENTRYP PFNGLCLIENTACTIVETEXTUREPROC)(GLenum texture);
typedef void (APIENTRYP PFNGLMULTITEXCOORD2FROC)(GLenum target, GLfloat s, GLfloat t);
#endif
static PFNGLACTIVETEXTUREARBPROC glActiveTexture = NULL;
//static PFNGLCLIENTACTIVETEXTUREPROC glClientActiveTexture = NULL;
//static PFNGLMULTITEXCOORD2FPROC glMultiTexCoord2f = NULL;

// Framebuffers
#ifndef GL_ARB_framebuffer_object
typedef GLboolean (APIENTRYP PFNGLISFRAMEBUFFERPROC)(GLuint framebuffer);
typedef void (APIENTRYP PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (APIENTRYP PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint *framebuffers);
typedef void (APIENTRYP PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint *framebuffers);
typedef GLenum (APIENTRYP PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (APIENTRYP PFNGLFRAMEBUFFERTEXTURE1DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (APIENTRYP PFNGLFRAMEBUFFERTEXTURE3DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset);
typedef void (APIENTRYP PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
typedef void (APIENTRYP PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC)(GLenum target, GLenum attachment, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNGLGENERATEMIPMAPPROC)(GLenum target);
#endif
//static PFNGLISFRAMEBUFFERPROC glIsFramebuffer = NULL;
static PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = NULL;
static PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = NULL;
static PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = NULL;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = NULL;
static PFNGLDRAWBUFFERSPROC glDrawBuffers = NULL;
//PFNGLFRAMEBUFFERTEXTURE1DPROC glFramebufferTexture1D = NULL;
static PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = NULL;
//PFNGLFRAMEBUFFERTEXTURE3DPROC glFramebufferTexture3D = NULL;
//PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer = NULL;
//PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC glGetFramebufferAttachmentParameteriv = NULL;
static PFNGLGENERATEMIPMAPPROC glGenerateMipmap = NULL;

// Sync Objects https://www.opengl.org/wiki/Sync_Object
#ifndef GL_ARB_sync
typedef GLsync (APIENTRYP PFNGLFENCESYNCPROC)(GLenum condition, GLbitfield flags);
typedef GLboolean (APIENTRYP PFNGLISSYNCPROC)(GLsync sync);
typedef void (APIENTRYP PFNGLDELETESYNCPROC)(GLsync sync);
typedef GLenum (APIENTRYP PFNGLCLIENTWAITSYNCPROC)(GLsync sync, GLbitfield flags, GLuint64 timeout);
typedef void (APIENTRYP PFNGLWAITSYNCPROC)(GLsync sync, GLbitfield flags, GLuint64 timeout);
#endif
static PFNGLFENCESYNCPROC glFenceSync = NULL;
static PFNGLISSYNCPROC glIsSync = NULL;
static PFNGLDELETESYNCPROC glDeleteSync = NULL;
static PFNGLCLIENTWAITSYNCPROC glClientWaitSync = NULL;
static PFNGLWAITSYNCPROC glWaitSync = NULL;

#endif // if !defined(USE_OSMESA) && ( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) )


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

static inline
bool
starts_with(const std::string &str,
            const std::string &prefix)
{
    return (str.substr( 0, prefix.size() ) == prefix);
}

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
        , _ctxCpuDriver(ShadertoyPlugin::eCPUDriverSoftPipe)
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
                    CPUDriverEnum cpuDriver,
                    void* buffer,
                    GLsizei width,
                    GLsizei height,
                    GLsizei rowLength,
                    GLboolean yUp)
    {
        bool newContext = false;

        if (!buffer) {
            //printf("%p before OSMesaMakeCurrent(%p,buf=NULL), OSMesaGetCurrentContext=%p\n", pthread_self(), _ctx, OSMesaGetCurrentContext());
            OSMesaMakeCurrent(_ctx, NULL, 0, 0, 0);

            return;
        }
        if ( !_ctx || ( (format      != _ctxFormat) ||
                        (depthBits   != _ctxDepthBits) ||
                        (stencilBits != _ctxStencilBits) ||
                        (accumBits   != _ctxAccumBits) ||
                        (cpuDriver   != _ctxCpuDriver) ) ) {
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
#if defined(OSMESA_GALLIUM_DRIVER) && (OSMESA_MAJOR_VERSION * 100 + OSMESA_MINOR_VERSION >= 1102)
            /* specify Z, stencil, accum sizes */
            {
                int attribs[100], n = 0;

                attribs[n++] = OSMESA_FORMAT;
                attribs[n++] = format;
                attribs[n++] = OSMESA_DEPTH_BITS;
                attribs[n++] = depthBits;
                attribs[n++] = OSMESA_STENCIL_BITS;
                attribs[n++] = stencilBits;
                attribs[n++] = OSMESA_ACCUM_BITS;
                attribs[n++] = accumBits;
                attribs[n++] = OSMESA_GALLIUM_DRIVER;
                attribs[n++] = (int)cpuDriver;
                attribs[n++] = 0;
                _ctx = OSMesaCreateContextAttribs( attribs, NULL );
            }
#else
#if OSMESA_MAJOR_VERSION * 100 + OSMESA_MINOR_VERSION >= 305
            /* specify Z, stencil, accum sizes */
            _ctx = OSMesaCreateContextExt( format, depthBits, stencilBits, accumBits, NULL );
#else
            _ctx = OSMesaCreateContext( format, NULL );
#endif
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
            _ctxCpuDriver = cpuDriver;
            newContext = true;
        }
        // optional: enable Gallium postprocess filters
#if OSMESA_MAJOR_VERSION * 100 + OSMESA_MINOR_VERSION >= 1000
        //OSMesaPostprocess(_ctx, const char *filter, unsigned enable_value);
#endif

        //printf("%p before OSMesaMakeCurrent(%p), OSMesaGetCurrentContext=%p\n", pthread_self(), _ctx, OSMesaGetCurrentContext());

        /* Bind the buffer to the context and make it current */
        if ( !OSMesaMakeCurrent( _ctx, buffer, type, width, height ) ) {
            DPRINT( ("OSMesaMakeCurrent failed!\n") );
            OFX::throwSuiteStatusException(kOfxStatFailed);

            return;
        }
        OSMesaPixelStore(OSMESA_Y_UP, yUp);
        OSMesaPixelStore(OSMESA_ROW_LENGTH, rowLength);
        if (newContext) {
            _effect->contextAttachedMesa(false);
            OpenGLContextData* contextData = &_openGLContextData;
            // force recompiling the shader
            contextData->imageShaderID = 0;
            contextData->imageShaderUniformsID = 0;
            contextData->haveAniso = glutExtensionSupported("GL_EXT_texture_filter_anisotropic");
            if (contextData->haveAniso) {
                GLfloat MaxAnisoMax;
                glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &MaxAnisoMax);
                contextData->maxAnisoMax = MaxAnisoMax;
                DPRINT( ("GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT = %f\n", contextData->maxAnisoMax) );
            } else {
                contextData->maxAnisoMax = 1.;
            }
        } else {
            // set viewport
            glViewport(0, 0, width, height);
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
    ShadertoyPlugin::CPUDriverEnum _ctxCpuDriver;
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
    delete ( (ShadertoyShader*)_openGLContextData.imageShader );
    _openGLContextData.imageShader = NULL;
}

#endif // USE_OPENGL


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

    assert(fs && vs);
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint param;
    glGetProgramiv(program, GL_LINK_STATUS, &param);
    if (param != GL_TRUE) {
        errstr = "Failed to link shader program\n";
        glCheckError();
        //glGetError();
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

    assert(vs);
    glDeleteShader(vs);
    assert(fs);
    glDeleteShader(fs);

#ifdef DEBUG
    {
        GLint i;
        GLint count;
        GLint size; // size of the variable
        GLenum type; // type of the variable (float, vec3 or mat4, etc)
        // GL_FLOAT, GL_FLOAT_VEC3, GL_FLOAT_MAT4
        GLint bufSize; // maximum name length

        std::vector<GLchar> name; // variable name in GLSL
        GLsizei length; // name length

        // Attributes
        glGetProgramiv(program,  GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &bufSize);
        name.resize(bufSize);
        count = 0;
        glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &count);
        if (count > 0) {
            DPRINT( ("Active Attributes: %d\n", count) );
        }
        for (i = 0; i < count; i++) {
            glGetActiveAttrib(program, (GLuint)i, bufSize, &length, &size, &type, &name[0]);
            glCheckError();
            DPRINT( ("Attribute #%d Type: %s Name: %s\n", i, glGetEnumString(type), &name[0]) );
        }

        // Uniforms
        glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &bufSize);
        name.resize(bufSize);
        count = 0;
        glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
        if (count > 0) {
            DPRINT( ("Active Uniforms: %d\n", count) );
        }
        for (i = 0; i < count; i++) {
            glGetActiveUniform(program, (GLuint)i, bufSize, &length, &size, &type, &name[0]);
            glCheckError();
            DPRINT( ("Uniform #%d Type: %s Name: %s\n", i, glGetEnumString(type), &name[0]) );
            GLint loc = glGetUniformLocation(program, &name[0]);
            if (loc >= 0) {
                switch (type) {
                case GL_FLOAT: {
                    GLfloat v;
                    glGetUniformfv(program, loc, &v);
                    DPRINT( ("Value: %g\n", v) );
                    break;
                }
                case GL_FLOAT_VEC2: {
                    GLfloat v[2];
                    glGetUniformfv(program, loc, v);
                    DPRINT( ("Value: (%g, %g)\n", v[0], v[1]) );
                    break;
                }
                case GL_FLOAT_VEC3: {
                    GLfloat v[3];
                    glGetUniformfv(program, loc, v);
                    DPRINT( ("Value: (%g, %g, %g)\n", v[0], v[1], v[2]) );
                    break;
                }
                case GL_FLOAT_VEC4: {
                    GLfloat v[4];
                    glGetUniformfv(program, loc, v);
                    DPRINT( ("Value: (%g, %g, %g, %g)\n", v[0], v[1], v[2], v[3]) );
                    break;
                }
                case GL_INT:
                case GL_BOOL: {
                    GLint v;
                    glGetUniformiv(program, loc, &v);
                    DPRINT( ("Value: %d\n", v) );
                    break;
                }
                case GL_INT_VEC2:
                case GL_BOOL_VEC2: {
                    GLint v[2];
                    glGetUniformiv(program, loc, v);
                    DPRINT( ("Value: (%d, %d)\n", v[0], v[1]) );
                    break;
                }
                case GL_INT_VEC3:
                case GL_BOOL_VEC3: {
                    GLint v[3];
                    glGetUniformiv(program, loc, v);
                    DPRINT( ("Value: (%d, %d, %d)\n", v[0], v[1], v[2]) );
                    break;
                }
                case GL_BOOL_VEC4:
                case GL_INT_VEC4: {
                    GLint v[4];
                    glGetUniformiv(program, loc, v);
                    DPRINT( ("Value: (%d, %d, %d, %d)\n", v[0], v[1], v[2], v[3]) );
                    break;
                }
                case GL_FLOAT_MAT2: {
                    GLfloat v[4];
                    glGetUniformfv(program, loc, v);
                    DPRINT( ("Value: (%g, %g, %g, %g)\n", v[0], v[1], v[2], v[3]) );
                    break;
                }
                case GL_FLOAT_MAT3: {
                    GLfloat v[9];
                    glGetUniformfv(program, loc, v);
                    DPRINT( ("Value: (%g, %g, %g, %g, %g, %g, %g, %g, %g)\n", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]) );
                    break;
                }
                case GL_FLOAT_MAT4: {
                    GLfloat v[16];
                    glGetUniformfv(program, loc, v);
                    DPRINT( ("Value: (%g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g)\n", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15]) );
                    break;
                }
                default:
                    break;
                } // switch
            }
        }
        glCheckError();
    }
#endif // ifdef DEBUG

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
   uniform float     iTime;                        // shader playback time (in seconds)
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
    "uniform float     iTime;\n"
    "uniform float     iTimeDelta;\n"
    "uniform int       iFrame;\n"
    "uniform float     iChannelTime[" STRINGISE (NBINPUTS) "];\n"
    "uniform vec3      iChannelResolution[" STRINGISE (NBINPUTS) "];\n"
    "uniform vec4      iMouse;\n"
    "uniform vec4      iDate;\n"
    "uniform float     iSampleRate;\n"
    "uniform vec2      ifFragCoordOffsetUniform;\n"
    "uniform vec2      iRenderScale;\n" // the OpenFX renderscale
    "uniform vec2      iChannelOffset[" STRINGISE (NBINPUTS) "];\n"
    "#define texture texture2D\n" // for some compatibility with newer Shadertoy>
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

#if (GL_ARB_framebuffer_object || GL_EXT_framebuffer_object) && !defined(GL_GLEXT_FUNCTION_POINTERS)
    const bool supportsMipmap = true;
#else
    const bool supportsMipmap = (bool)glGenerateMipmap;
#endif

#ifdef DEBUG_TIME
    struct timeval t1, t2;
    gettimeofday(&t1, NULL);
#endif

# ifdef OFX_SUPPORTS_OPENGLRENDER
    const OFX::ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    //DPRINT( ("render: openGLSuite %s\n", gHostDescription.supportsOpenGLRender ? "found" : "not found") );
    if (gHostDescription.supportsOpenGLRender) {
        DPRINT( ("render: openGL rendering %s\n", args.openGLEnabled ? "enabled" : "DISABLED") );
    }
# endif

    const OfxRectI& renderWindow = args.renderWindow;
    //DPRINT( ("Render: window = [%d, %d - %d, %d]\n", renderWindow.x1, renderWindow.y1, renderWindow.x2, renderWindow.y2) );


    // get the output image texture
    OFX::auto_ptr<OFX::ImageBase> dst;
    if (args.openGLEnabled) {
        dst.reset(_dstClip->loadTexture(time));
    } else {
        dst.reset(_dstClip->fetchImage(time));
    }
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
# if defined(USE_OPENGL) && defined(DEBUG)
    if (args.openGLEnabled) {
        const GLuint dstIndex = (GLuint)((OFX::Texture*)dst.get())->getIndex();
        const GLenum dstTarget = (GLenum)((OFX::Texture*)dst.get())->getTarget();
        DPRINT( ( "openGL: output texture index %d, target 0x%04X, depth %s\n",
                  dstIndex, dstTarget, mapBitDepthEnumToStr(dstBitDepth) ) );
    }
# endif

    bool inputEnable[NBINPUTS];
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        inputEnable[i] = _inputEnable[i]->getValue();
    }

    OFX::auto_ptr<const OFX::ImageBase> src[NBINPUTS];
    if (args.openGLEnabled) {
        for (unsigned i = 0; i < NBINPUTS; ++i) {
            src[i].reset( ( inputEnable[i] && _srcClips[i] && _srcClips[i]->isConnected() ) ?
                          _srcClips[i]->loadTexture(time) : 0 );
        }
    } else {
        for (unsigned i = 0; i < NBINPUTS; ++i) {
            src[i].reset( ( inputEnable[i] && _srcClips[i] && _srcClips[i]->isConnected() ) ?
                          _srcClips[i]->fetchImage(time) : 0 );
        }
    }

    std::vector<OFX::BitDepthEnum> srcBitDepth(NBINPUTS, OFX::eBitDepthNone);
    std::vector<OFX::PixelComponentEnum> srcComponents(NBINPUTS, OFX::ePixelComponentNone);
    std::vector<GLenum> srcTarget(NBINPUTS, GL_TEXTURE_2D);
    std::vector<GLuint> srcIndex(NBINPUTS);
    std::vector<FilterEnum> filter(NBINPUTS, eFilterNearest);
    std::vector<WrapEnum> wrap(NBINPUTS, eWrapRepeat);
    GLuint dstFrameBuffer = 0;
    GLuint dstTarget = GL_TEXTURE_2D;
    GLuint dstIndex = 0;
    GLenum format = 0;
    GLenum type = 0;
    GLint depthBits = 0;
#ifdef USE_OSMESA
    GLint stencilBits = 0;
    GLint accumBits = 0;
#endif

    for (unsigned i = 0; i < NBINPUTS; ++i) {
        if ( src[i].get() ) {
            srcBitDepth[i] = src[i]->getPixelDepth();
            srcComponents[i] = src[i]->getPixelComponents();
            if ( (srcBitDepth[i] != dstBitDepth) || (srcComponents[i] != dstComponents) ) {
                OFX::throwSuiteStatusException(kOfxStatErrImageFormat);

                return;
            }
            // filter for each texture (nearest, linear, mipmap [default])
            // nearest = GL_NEAREST/GL_NEAREST
            // linear = GL_LINEAR/GL_LINEAR
            // mipmap = GL_LINEAR_MIPMAP_LINEAR/GL_LINEAR
            // Some shaders depend on to filter, so leave it as it is
            filter[i] = /*args.renderQualityDraft ? eFilterNearest :*/ (FilterEnum)_inputFilter[i]->getValueAtTime(time);

            // wrap for each texture (repeat [default], clamp, mirror)
            // clamp = GL_CLAMP_TO_EDGE
            wrap[i] = (WrapEnum)_inputWrap[i]->getValueAtTime(time);

# ifdef USE_OPENGL
            if (args.openGLEnabled) {
                srcIndex[i] = (GLuint)((OFX::Texture*)src[i].get())->getIndex();
                srcTarget[i] = (GLenum)((OFX::Texture*)src[i].get())->getTarget();
                DPRINT( ( "openGL: source texture %u index %d, target 0x%04X, depth %s\n",
                          i, srcIndex[i], srcTarget[i], mapBitDepthEnumToStr(srcBitDepth[i]) ) );
            }
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

    //const OfxRectI dstBounds = dst->getBounds();
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
#ifdef USE_OSMESA
    /* Allocate the image buffer */
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
    CPUDriverEnum cpuDriver = eCPUDriverSoftPipe;
    if (_cpuDriver) {
        cpuDriver = (CPUDriverEnum)_cpuDriver->getValueAtTime(time);
    }
    // we pass the address of the first pixel, which depends on the sign of rowBytes
    GLsizei bufferWidth = renderWindow.x2 - renderWindow.x1;
    GLsizei bufferHeight = renderWindow.y2 - renderWindow.y1;
    GLsizei bufferRowLength = std::abs( dst->getRowBytes() ) / dst->getPixelBytes();
    GLboolean bufferYUp = (dst->getRowBytes() > 0);
    void* buffer = bufferYUp ? ((OFX::Image*)dst.get())->getPixelAddress(renderWindow.x1, renderWindow.y1) : ((OFX::Image*)dst.get())->getPixelAddress(renderWindow.x1, renderWindow.y2 - 1);
    osmesa->setContext(format, depthBits, type, stencilBits, accumBits, cpuDriver, buffer, bufferWidth, bufferHeight, bufferRowLength, bufferYUp);
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

    {
        AutoMutex lock( _rendererInfoMutex.get() );
        std::string &message = _rendererInfo;
        if ( message.empty() ) {
            const char* glRenderer = (const char*)glGetString(GL_RENDERER);
            const char* glVersion = (const char*)glGetString(GL_VERSION);
            const char* glVendor = (const char*)glGetString(GL_VENDOR);
            const char* glSlVersion = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
            const char* glExtensions = (const char*)glGetString(GL_EXTENSIONS);
            message += "OpenGL renderer information:";
            message += "\nGL_RENDERER = ";
            message += glRenderer ? glRenderer : "N/A";
            message += "\nGL_VERSION = ";
            message += glVersion ? glVersion : "N/A";
            message += "\nGL_VENDOR = ";
            message += glVendor ? glVendor : "N/A";
            message += "\nGL_SHADING_LANGUAGE_VERSION = ";
            message += glSlVersion ? glSlVersion : "N/A";
            message += "\nGL_EXTENSIONS = ";
            message += glExtensions ? glExtensions :  "N/A";
        }
    }


    // compile and link the shader if necessary
    bool imageShaderParamsUpdated = false;
    ShadertoyShader *shadertoy;
    {
        AutoMutex lock( _imageShaderMutex.get() );
        bool must_recompile = false;
        bool uniforms_changed = false;
        shadertoy = (ShadertoyShader *)contextData->imageShader;
        assert(shadertoy);
        must_recompile = (_imageShaderID != contextData->imageShaderID) || _imageShaderUpdateParams;
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
            {
                // for compatibility with ShaderToy, remove the first line that starts with "const vec2 iRenderScale"
                std::size_t found = str.find("const vec2 iRenderScale");
                if ( (found != std::string::npos) && ( (found == 0) || ( (str[found - 1] == '\n') || (str[found - 1] == '\r') ) ) ) {
                    std::size_t eol = str.find('\n', found);
                    if (eol == std::string::npos) {
                        // last line
                        eol = str.size();
                    }
                    // replace by an empty line
                    str.replace( found, eol - found, std::string() );
                }
            }
            {
                // for compatibility with ShaderToy, remove the first line that starts with "const vec2 iChannelOffset"
                std::size_t found = str.find("const vec2 iChannelOffset");
                if ( (found != std::string::npos) && ( (found == 0) || ( (str[found - 1] == '\n') || (str[found - 1] == '\r') ) ) ) {
                    std::size_t eol = str.find('\n', found);
                    if (eol == std::string::npos) {
                        // last line
                        eol = str.size();
                    }
                    // replace by an empty line
                    str.replace( found, eol - found, std::string() );
                }
            }
            std::string fsSource = fsHeader;
            for (unsigned i = 0; i < NBINPUTS; ++i) {
                fsSource += std::string("uniform sampler2D iChannel") + (char)('0' + i) + ";\n";
            }
            fsSource += "#line 0\n";
            fsSource += str + '\n' + fsFooter;
            std::string errstr;
            const char* fragmentShader = fsSource.c_str();
            shadertoy->program = compileAndLinkProgram(vsSource.c_str(), fragmentShader, errstr);
            const GLuint program = shadertoy->program;
            if (shadertoy->program == 0) {
                setPersistentMessage(OFX::Message::eMessageError, "", "Failed to compile and link program");
                sendMessage( OFX::Message::eMessageError, "", errstr.c_str() );
                OFX::throwSuiteStatusException(kOfxStatFailed);

                return;
            }
            shadertoy->iResolutionLoc        = glGetUniformLocation(program, "iResolution");
            shadertoy->iTimeLoc        = glGetUniformLocation(program, "iTime");
            if (shadertoy->iTimeLoc == -1) {
                // for backward compatibility with older (pre-0.9.3) shaders
                shadertoy->iTimeLoc        = glGetUniformLocation(program, "iGlobalTime");
            }
            shadertoy->iTimeDeltaLoc         = glGetUniformLocation(program, "iTimeDelta");
            shadertoy->iFrameLoc             = glGetUniformLocation(program, "iFrame");
            shadertoy->iChannelTimeLoc       = glGetUniformLocation(program, "iChannelTime");
            shadertoy->iMouseLoc             = glGetUniformLocation(program, "iMouse");
            shadertoy->iDateLoc              = glGetUniformLocation(program, "iDate");
            shadertoy->iSampleRateLoc        = glGetUniformLocation(program, "iSampleRate");
            shadertoy->iChannelResolutionLoc = glGetUniformLocation(program, "iChannelResolution");
            shadertoy->ifFragCoordOffsetUniformLoc = glGetUniformLocation(program, "ifFragCoordOffsetUniform");
            shadertoy->iRenderScaleLoc = glGetUniformLocation(program, "iRenderScale");
            shadertoy->iChannelOffsetLoc = glGetUniformLocation(program, "iChannelOffset");
            char iChannelX[10] = "iChannelX"; // index 8 holds the channel character
            assert(NBINPUTS < 10 && iChannelX[8] == 'X');
            for (unsigned i = 0; i < NBINPUTS; ++i) {
                iChannelX[8] = '0' + i;
                shadertoy->iChannelLoc[i] = glGetUniformLocation(shadertoy->program, iChannelX);
                //printf("%s -> %d\n", iChannelX, (int)shadertoy->iChannelLoc[i]);
            }

            if (_imageShaderUpdateParams) {
                _imageShaderHasMouse = false;

                _imageShaderExtraParameters.clear();
                {
                    // go through the uniforms, and list extra parameters

                    GLint i;
                    GLint count;
                    GLint size; // size of the variable
                    GLenum type; // type of the variable (float, vec3 or mat4, etc)
                    GLint bufSize; // maximum name length

                    std::string name; // variable name in GLSL
                    GLsizei length; // name length

                    // Uniforms
                    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &bufSize);
                    count = 0;
                    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
                    if (count > 0) {
                        //DPRINT( ("Active Uniforms: %d\n", count) );
                    }
                    _imageShaderInputEnabled.assign(NBINPUTS, false);
                    for (i = 0; i < count; i++) {
                        name.resize(bufSize);
                        glGetActiveUniform(program, (GLuint)i, bufSize, &length, &size, &type, &name[0]);
                        glCheckError();
                        name.resize(length);
                        //DPRINT( ("Uniform #%d Type: %s Name: %s\n", i, glGetEnumString(type), &name[0]) );
                        GLint loc = glGetUniformLocation(program, &name[0]);

                        if (loc >= 0) {
                            if ( starts_with(name, "iChannel") ) {
                                for (unsigned j = 0; j < NBINPUTS; ++j) {
                                    if ( name == ( std::string("iChannel") + (char)('0' + j) ) ) {
                                        _imageShaderInputEnabled[j] = true;
                                        getChannelInfo(fragmentShader, j, _imageShaderInputLabel[j], _imageShaderInputHint[j], _imageShaderInputFilter[j], _imageShaderInputWrap[j]);
                                        loc = -1; // go to next uniform
                                        break;
                                    }
                                }
                            }

                            // mark if shader has mouse params
                            if (name == "iMouse") {
                                _imageShaderHasMouse = true;
                                continue;
                            }

                            if ( (name == "iResolution") ||
                                 ( name == "iTime") ||
                                 ( name == "iGlobalTime") ||
                                 ( name == "iTimeDelta") ||
                                 ( name == "iFrame") ||
                                 ( name == "iChannelTime") ||
                                 ( name == "iChannelTime[0]") ||
                                 //name == "iMouse" ||
                                 ( name == "iDate") ||
                                 ( name == "iSampleRate") ||
                                 ( name == "iChannelResolution") ||
                                 ( name == "iChannelResolution[0]") ||
                                 ( name == "ifFragCoordOffsetUniform") ||
                                 ( name == "iRenderScale") ||
                                 ( name == "iChannelOffset") ||
                                 ( name == "iChannelOffset[0]") ||
                                 starts_with(name, "gl_") ) {
                                // builtin uniform
                                continue;
                            }
                            UniformTypeEnum t = eUniformTypeNone;
                            switch (type) {
                            case GL_BOOL:
                                t = eUniformTypeBool;
                                break;

                            case GL_INT:
                                t = eUniformTypeInt;
                                break;

                            case GL_FLOAT:
                                t = eUniformTypeFloat;
                                break;

                            case GL_FLOAT_VEC2:
                                t = eUniformTypeVec2;
                                break;

                            case GL_FLOAT_VEC3:
                                t = eUniformTypeVec3;
                                break;

                            case GL_FLOAT_VEC4:
                                t = eUniformTypeVec4;
                                break;

                            default:
                                // ignore uniform
                                break;
                            }
                            if (t == eUniformTypeNone) {
                                DPRINT( ("Uniform #%d Type: %s Name: %s NOT SUPPORTED\n", i, glGetEnumString(type), &name[0]) );
                                continue;
                            }

                            ExtraParameter p;
                            p.init(t, name);

                            switch (t) {
                            case eUniformTypeBool: {
                                GLint v;
                                glGetUniformiv(program, loc, &v);
                                //DPRINT( ("Value: %d\n", v) );
                                p.set(p.getDefault(), (bool)v);
                                break;
                            }
                            case eUniformTypeInt: {
                                GLint v;
                                glGetUniformiv(program, loc, &v);
                                //DPRINT( ("Value: %d\n", v) );
                                p.set(p.getDefault(), (int)v);
                                break;
                            }
                            case eUniformTypeFloat: {
                                GLfloat v;
                                glGetUniformfv(program, loc, &v);
                                //DPRINT( ("Value: %g\n", v) );
                                p.set(p.getDefault(), (float)v);
                                break;
                            }
                            case eUniformTypeVec2: {
                                GLfloat v[2];
                                glGetUniformfv(program, loc, v);
                                //DPRINT( ("Value: (%g, %g)\n", v[0], v[1]) );
                                p.set(p.getDefault(), (float)v[0], (float)v[1]);
                                break;
                            }
                            case eUniformTypeVec3: {
                                GLfloat v[3];
                                glGetUniformfv(program, loc, v);
                                //DPRINT( ("Value: (%g, %g, %g)\n", v[0], v[1], v[2]) );
                                p.set(p.getDefault(), (float)v[0], (float)v[1], (float)v[2]);
                                break;
                            }
                            case eUniformTypeVec4: {
                                GLfloat v[4];
                                glGetUniformfv(program, loc, v);
                                //DPRINT( ("Value: (%g, %g, %g, %g)\n", v[0], v[1], v[2], v[3]) );
                                p.set(p.getDefault(), (float)v[0], (float)v[1], (float)v[2], (float)v[3]);
                                break;
                            }
                            default:
                                assert(false);
                                break;
                            }

                            // parse hint/min/max from comment
                            getExtraParameterInfo(fragmentShader, p);

                            _imageShaderExtraParameters.push_back(p);
                        } // if (loc >= 0)
                        _imageShaderBBox = (BBoxEnum)_bbox->getValueAtTime(time);
                        getBboxInfo(fragmentShader, _imageShaderBBox);
                    } // for (i = 0; i < count; i++) {
                }

                std::sort(_imageShaderExtraParameters.begin(), _imageShaderExtraParameters.end(), ExtraParameter::less_than_pos());
                imageShaderParamsUpdated = true;
            } // if (_imageShaderUpdateParams)

            // Note: InstanceChanged is (illegally) triggered at the end of render() using:
            // _imageShaderParamsUpdated->setValue( !_imageShaderParamsUpdated->getValueAtTime(time) );
            // (setValue is normally not authorized from render())
            // Mark that setValue has to be called at the end of render():
            _imageShaderCompiled = true;
        }
        if (must_recompile || uniforms_changed) {
            std::fill(shadertoy->iParamLoc, shadertoy->iParamLoc + NBUNIFORMS, -1);
            unsigned paramCount = std::max( 0, std::min(_paramCount->getValue(), (int)_paramType.size()) );
            for (unsigned i = 0; i < paramCount; ++i) {
                std::string paramName;
                _paramName[i]->getValue(paramName);
                if ( !paramName.empty() ) {
                    shadertoy->iParamLoc[i] = glGetUniformLocation( shadertoy->program, paramName.c_str() );
                }
            }
        }
    }
    glCheckError();

    if (!args.openGLEnabled) {
        // load the source image into a texture
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glActiveTexture(GL_TEXTURE0);

        GLenum internalFormat = format;
        switch (format) {
        case GL_ALPHA:
            switch (type) {
            case GL_UNSIGNED_BYTE:
                internalFormat = GL_ALPHA8;
                break;
            case GL_UNSIGNED_SHORT:
                internalFormat = GL_ALPHA16;
                break;
            case GL_FLOAT:
                internalFormat = GL_ALPHA32F_ARB;
                break;
            case GL_HALF_FLOAT_ARB:
                internalFormat = GL_ALPHA16F_ARB;
                break;
            default:
                //format/type combo not supported
                break;
            }
            break;
        case GL_LUMINANCE:
            switch (type) {
            case GL_UNSIGNED_BYTE:
                internalFormat = GL_R8;// GL_LUMINANCE8;
                break;
            case GL_UNSIGNED_SHORT:
                internalFormat = GL_LUMINANCE16;
                break;
            case GL_FLOAT:
                internalFormat = GL_LUMINANCE32F_ARB;
                break;
            case GL_HALF_FLOAT_ARB:
                internalFormat = GL_LUMINANCE16F_ARB;
                break;
            default:
                //format/type combo not supported
                break;
            }
            break;
        case GL_LUMINANCE_ALPHA:
            switch (type) {
            case GL_UNSIGNED_BYTE:
                internalFormat = GL_RG8;// GL_LUMINANCE8_ALPHA8;
                break;
            case GL_UNSIGNED_SHORT:
                internalFormat = GL_LUMINANCE16_ALPHA16;
                break;
            case GL_FLOAT:
                internalFormat = GL_LUMINANCE_ALPHA32F_ARB;
                break;
            case GL_HALF_FLOAT_ARB:
                internalFormat = GL_LUMINANCE_ALPHA16F_ARB;
                break;
            default:
                //format/type combo not supported
                break;
            }
            break;
        case GL_RGB:
            switch (type) {
            case GL_UNSIGNED_BYTE:
                internalFormat = GL_RGB8;
                break;
            case GL_UNSIGNED_SHORT:
                internalFormat = GL_RGB16;
                break;
            case GL_FLOAT:
                internalFormat = GL_RGB32F_ARB;
                break;
            case GL_HALF_FLOAT_ARB:
                internalFormat = GL_RGB16F_ARB;
                break;
            default:
                //format/type combo not supported
                break;
            }
            break;
        case GL_RGBA:
            switch (type) {
            case GL_UNSIGNED_BYTE:
                internalFormat = GL_RGBA8;
                break;
            case GL_UNSIGNED_SHORT:
                internalFormat = GL_RGBA16;
                break;
            case GL_FLOAT:
                internalFormat = GL_RGBA32F_ARB;
                break;
            case GL_HALF_FLOAT_ARB:
                internalFormat = GL_RGBA16F_ARB;
                break;
            default:
                break;
                //format/type combo not supported
            }
        default:
            //bad format
            break;
        }

#ifdef USE_OPENGL
        // create a framebuffer to render to (OpenGL only)
        glGenFramebuffers(1, &dstFrameBuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, dstFrameBuffer);

        OfxRectI dstBounds = dst->getBounds();
        glGenTextures(1, &dstIndex);
        glBindTexture(dstTarget, dstIndex);
        glTexImage2D(dstTarget, 0, internalFormat, dstBounds.x2 - dstBounds.x1,
                     dstBounds.y2 - dstBounds.y1, 0, format, type, NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, dstTarget, dstIndex, 0);

        GLenum buf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &buf);
        glCheckError();
#endif

        // Non-power-of-two textures are supported if the GL version is 2.0 or greater, or if the implementation exports the GL_ARB_texture_non_power_of_two extension. (Mesa does, of course)
        for (unsigned i = 0; i < NBINPUTS; ++i) {
            if ( src[i].get() && (shadertoy->iChannelLoc[i] >= 0) ) {
                glGenTextures(1, &srcIndex[i]);
                OfxRectI srcBounds = src[i]->getBounds();
                glBindTexture(srcTarget[i], srcIndex[i]);
                // legacy mipmap generation was replaced by glGenerateMipmap from GL_ARB_framebuffer_object (see below)
                if ( ( (filter[i] == eFilterMipmap) || (filter[i] == eFilterAnisotropic) ) && !supportsMipmap ) {
                    DPRINT( ("Shadertoy: legacy mipmap generation!\n") );
                    // this must be done before glTexImage2D
                    glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
                    // requires extension GL_SGIS_generate_mipmap or OpenGL 1.4.
                    glTexParameteri(srcTarget[i], GL_GENERATE_MIPMAP, GL_TRUE); // Allocate the mipmaps
                }
                glTexImage2D( srcTarget[i], 0, internalFormat,
                              srcBounds.x2 - srcBounds.x1, srcBounds.y2 - srcBounds.y1, 0,
                              format, type, ((OFX::Image*)src[i].get())->getPixelData() );
                glBindTexture(srcTarget[i], 0);
            }
        }
        glCheckError();
    }

    bool haveAniso = contextData->haveAniso;
    float maxAnisoMax = contextData->maxAnisoMax;
    int w = (renderWindow.x2 - renderWindow.x1);
    int h = (renderWindow.y2 - renderWindow.y1);

    // setup the projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClear(GL_DEPTH_BUFFER_BIT); // does not hurt, even if there is no Z-buffer (Sony Catalyst)
    glCheckError();

    double fps = _dstClip->getFrameRate();
    if (fps <= 0) {
        fps = 1.;
    }
    GLfloat t = time / fps;
    const OfxPointD& rs = args.renderScale;
    OfxRectI dstBoundsFull;
    OFX::Coords::toPixelEnclosing(_dstClip->getRegionOfDefinition(time), rs, _dstClip->getPixelAspectRatio(), &dstBoundsFull);

    glUseProgram(shadertoy->program);
    glCheckError();

    // Uniform locations may be -1 if the Uniform was optimised out by the compîler.
    // see https://www.opengl.org/wiki/GLSL_:_common_mistakes#glGetUniformLocation_and_glGetActiveUniform
    if (shadertoy->iResolutionLoc >= 0) {
        double width = dstBoundsFull.x2 - dstBoundsFull.x1;
        double height = dstBoundsFull.y2 - dstBoundsFull.y1;
        // last coord is 1.
        // see https://github.com/beautypi/shadertoy-iOS-v2/blob/a852d8fd536e0606377a810635c5b654abbee623/shadertoy/ShaderPassRenderer.m#L329
        glUniform3f (shadertoy->iResolutionLoc, width, height, 1.);
    }
    if (shadertoy->iTimeLoc >= 0) {
        glUniform1f (shadertoy->iTimeLoc, t);
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
            if ( src[i].get() ) {
                OfxRectI srcBounds = src[i]->getBounds();
                rv[i * 3] = srcBounds.x2 - srcBounds.x1;
                rv[i * 3 + 1] = srcBounds.y2 - srcBounds.y1;
            } else {
                rv[i * 3] = rv[i * 3 + 1] = 0;
            }
            // last coord is 1.
            // see https://github.com/beautypi/shadertoy-iOS-v2/blob/a852d8fd536e0606377a810635c5b654abbee623/shadertoy/ShaderPassRenderer.m#L329
            rv[i * 3 + 2] = 1;
        }
        glUniform3fv(shadertoy->iChannelResolutionLoc, NBINPUTS, rv);
    }
    if (shadertoy->iMouseLoc >= 0) {
        // mouse parameters, see:
        // https://www.shadertoy.com/view/Mss3zH
        // https://www.shadertoy.com/view/4sf3RN
        // https://www.shadertoy.com/view/XsGSDz

        double x, y, xc, yc;
        if ( !_mouseParams->getValueAtTime(time) ) {
            x = y = xc = yc = 0.;
        } else {
            _mousePosition->getValueAtTime(time, x, y);
            _mouseClick->getValueAtTime(time, xc, yc);
            if ( !_mousePressed->getValueAtTime(time) ) {
                // negative is mouse released
                // see https://github.com/beautypi/shadertoy-iOS-v2/blob/a852d8fd536e0606377a810635c5b654abbee623/shadertoy/ShaderCanvasViewController.m#L315
                xc = -xc;
                yc = -yc;
            }
        }
        glUniform4f (shadertoy->iMouseLoc, x * rs.x, y * rs.y, xc * rs.x, yc * rs.y);
    }
    unsigned paramCount = std::max( 0, std::min(_paramCount->getValue(), (int)_paramType.size()) );
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
    glCheckError();
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        if ( src[i].get() && (shadertoy->iChannelLoc[i] >= 0) ) {
            glUniform1i(shadertoy->iChannelLoc[i], i);
            glBindTexture(srcTarget[i], srcIndex[i]);
            glEnable(srcTarget[i]);

            // GL_ARB_framebuffer_object
            // https://www.opengl.org/wiki/Common_Mistakes#Automatic_mipmap_generation
            if ( ( (filter[i] == eFilterMipmap) || (filter[i] == eFilterAnisotropic) ) && supportsMipmap ) {
                glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
#if GL_ARB_framebuffer_object || defined(GL_GLEXT_FUNCTION_POINTERS)
                glGenerateMipmap(GL_TEXTURE_2D);  //Generate mipmaps now!!!
#else
                glGenerateMipmapEXT(GL_TEXTURE_2D);  //Generate mipmaps now!!!
#endif
                glCheckError();
            }
            GLenum min_filter = GL_NEAREST;
            GLenum mag_filter = GL_NEAREST;
            switch (filter[i]) {
            case eFilterNearest:
                min_filter = GL_NEAREST;
                mag_filter = GL_NEAREST;
                break;
            case eFilterLinear:
                min_filter = GL_LINEAR;
                mag_filter = GL_LINEAR;
                break;
            case eFilterMipmap:
                min_filter = GL_LINEAR_MIPMAP_LINEAR;
                mag_filter = GL_LINEAR;
                break;
            case eFilterAnisotropic:
                min_filter = GL_LINEAR_MIPMAP_LINEAR;
                mag_filter = GL_LINEAR;
                if (haveAniso) {
                    glTexParameterf(srcTarget[i], GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAnisoMax);
                }
                break;
            }
            glTexParameteri(srcTarget[i], GL_TEXTURE_MIN_FILTER, min_filter);
            glTexParameteri(srcTarget[i], GL_TEXTURE_MAG_FILTER, mag_filter);

            GLenum wrapst = (wrap[i] == eWrapClamp) ? GL_CLAMP_TO_EDGE : ( (wrap[i] == eWrapMirror) ? GL_MIRRORED_REPEAT : GL_REPEAT );
            glTexParameteri(srcTarget[i], GL_TEXTURE_WRAP_S, wrapst);
            glTexParameteri(srcTarget[i], GL_TEXTURE_WRAP_T, wrapst);

            // The texture parameters vflip and srgb [default = false] should be handled by the reader
        } else {
            glBindTexture(srcTarget[i], 0);
        }
    }
    glCheckError();
    if (shadertoy->iDateLoc >= 0) {
        // https://www.shadertoy.com/view/4sf3RN
        // month starts at 0
        // day starts at 1
        // time in seconds is from 0 to 86400 (24*60*60)
        // do not use the current date, as it may generate a different image at each render
        double year, month, day, seconds;
        _date->getValueAtTime(time, year, month, day, seconds);
        year = std::floor(year);
        month = std::floor(month);
        day = std::floor(day);
        seconds += t;
        int dayincr = std::floor(seconds / (24*60*60));
        seconds = seconds - dayincr * (24*60*60);
        day += dayincr;
        if (month == 0 || // jan
            month == 2 || // mar
            month == 4 || // mai
            month == 6 || // jul
            month == 7 || // aug
            month == 9 || // oct
            month == 11) { // dec
            if (day > 31) {
                day -= 31;
                month = ((int)month + 1) % 12;
            }
        } else if (month == 3 || // apr
                   month == 5 || // jun
                   month == 8 || // sep
                   month == 10) { // nov
            if (day > 30) {
                day -= 30;
                month = ((int)month + 1) % 12;
            }
        } else if (month == 1) { // feb
            if (day > 28) { // don't care about leap years
                day -= 28;
                month = ((int)month + 1) % 12;
            }
        }
        glUniform4f(shadertoy->iDateLoc, year, month, day, seconds);
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
    if (shadertoy->iChannelOffsetLoc >= 0) {
        GLfloat rv[2 * NBINPUTS];
        if ( src[0].get() ) {
            OfxRectI srcBounds = src[0]->getBounds();
            rv[0] = srcBounds.x1;
            rv[1] = srcBounds.y1;
        } else {
            rv[0] = 0;
            rv[1] = 0;
        }
        for (unsigned i = 1; i < NBINPUTS; ++i) {
            if ( src[i].get() ) {
                OfxRectI srcBounds = src[i]->getBounds();
                rv[i * 2] = srcBounds.x1 - rv[0];
                rv[i * 2 + 1] = srcBounds.y1 - rv[1];
            } else {
                rv[i * 2] = rv[i * 2 + 1] = 0;
            }
        }
        glUniform2fv(shadertoy->iChannelOffsetLoc, NBINPUTS, rv);
    }
    glCheckError();

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glDepthFunc(GL_LESS);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glCheckError();

    // Are your images pretty large? Maybe one approach would to render the scene in tiled chunks.
    // For example, divide the window into an NxM grid of tiles, then render the scene into each tile with glScissor.
    // After rendering each tile, check if there's user input and abort drawing the grid if needed.
    // Ideally the tile size should be a multiple of llvmpipe's tile size which is 64x64.
    // llvmpipe employs multiple threads to process tiles in parallel so your tiles should probably
    // be 128x128 for 4 cores, 256x128 for 8 cores, etc.
    int tile_w;
    int tile_h;
#ifdef USE_OSMESA
    {
        int nCPUs = OFX::MultiThread::getNumCPUs();
        // - take the square root of nCPUs
        // - compute the next closest power of two -> this gives the number of tiles for the x dimension
        int pow2_x = std::ceil(std::log( std::sqrt(nCPUs) ) / M_LN2);
        tile_w = 64 * (1 << pow2_x);
        // - compute the next power of two for the other side
        int pow2_y = std::ceil(std::log( nCPUs / (double)(1 << pow2_x) ) / M_LN2);
        tile_h = 64 * (1 << pow2_y);
        //DPRINT( ("Shadertoy: tile size: %d %d for %d CPUs\n", tile_w, tile_h, nCPUs) );
    }
#else
    tile_w = w;
    tile_h = h;
#endif

#ifdef USE_OPENGL
    if (!args.openGLEnabled) {
        glBindFramebuffer(GL_FRAMEBUFFER, dstFrameBuffer);
        glViewport(0, 0, w, h);
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
            DPRINT( ( "rendering tile: %d %d %d %d took %d us\n", x1, y1, tile_w, tile_h, 1000000 * (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) ) );
#endif
        }
    }
    if (aborted) {
        DPRINT( ("Shadertoy: aborted!\n") );
    }
    glCheckError();

    for (unsigned i = 0; i < NBINPUTS; ++i) {
        if (shadertoy->iChannelLoc[i] >= 0) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(srcTarget[i], 0);
        }
    }
    glCheckError();

    glUseProgram(0);
    glCheckError();

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

    if (!args.openGLEnabled) {
        /* This is very important!!!
         * Make sure buffered commands are finished!!!
         */
        for (unsigned i = 0; i < NBINPUTS; ++i) {
            if ( src[i].get() ) {
                glDeleteTextures(1, &srcIndex[i]);
            }
        }

        if (!aborted) {
            glFlush(); // waits until commands are submitted but does not wait for the commands to finish executing
            glFinish(); // waits for all previously submitted commands to complete executing
        }
        glCheckError();

#ifdef USE_OPENGL
        if (!aborted) {
            // Copy pixels back into the destination.
            glReadPixels(0, 0, w, h, format, type, ((OFX::Image*)dst.get())->getPixelAddress(renderWindow.x1, renderWindow.y1));
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // Free the framebuffer and its resources.
        glDeleteTextures(1, &dstIndex);
        glDeleteFramebuffers(1, &dstFrameBuffer);
        glCheckError();
#endif
    }

#ifdef USE_OSMESA
    // make sure the buffer is not referenced anymore
    osmesa->setContext(format, depthBits, type, stencilBits, accumBits, cpuDriver, NULL, 0, 0, 0, true);
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
    DPRINT( ( "rendering took %d us\n", 1000000 * (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) ) );
#endif
    if (imageShaderParamsUpdated) {
        // Note: InstanceChanged is (illegally) triggered at the end of render() using:
        _imageShaderParamsUpdated->setValue( !_imageShaderParamsUpdated->getValueAtTime(time) );
        // (setValue is normally not authorized from render())
    }
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

#endif

    {
        AutoMutex lock( _rendererInfoMutex.get() );
        std::string &message = _rendererInfo;
        if ( message.empty() ) {
            const char* glRenderer = (const char*)glGetString(GL_RENDERER);
            const char* glVersion = (const char*)glGetString(GL_VERSION);
            const char* glVendor = (const char*)glGetString(GL_VENDOR);
            const char* glSlVersion = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
            const char* glExtensions = (const char*)glGetString(GL_EXTENSIONS);
            message += "OpenGL renderer information:";
            message += "\nGL_RENDERER = ";
            message += glRenderer ? glRenderer : "N/A";
            message += "\nGL_VERSION = ";
            message += glVersion ? glVersion : "N/A";
            message += "\nGL_VENDOR = ";
            message += glVendor ? glVendor : "N/A";
            message += "\nGL_SHADING_LANGUAGE_VERSION = ";
            message += glSlVersion ? glSlVersion : "N/A";
            message += "\nGL_EXTENSIONS = ";
            message += glExtensions ? glExtensions :  "N/A";
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

#ifdef USE_OPENGL
#ifdef DEBUG
    if (OFX::getImageEffectHostDescription()->isNatron && !createContextData) {
        DPRINT( ("ERROR: Natron did not ask to create context data\n") );
    }
#endif
    OpenGLContextData* contextData = &_openGLContextData;
    assert(contextData->imageShader);
    if (createContextData) {
        contextData = new OpenGLContextData;
        contextData->imageShader = new ShadertoyShader;
    }
    assert(contextData->imageShader);
    // force recompiling the shader
    contextData->imageShaderID = 0;
    contextData->imageShaderUniformsID = 0;
    contextData->haveAniso = glutExtensionSupported("GL_EXT_texture_filter_anisotropic");
    if (contextData->haveAniso) {
        GLfloat MaxAnisoMax;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &MaxAnisoMax);
        contextData->maxAnisoMax = MaxAnisoMax;
        DPRINT( ("GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT = %f\n", contextData->maxAnisoMax) );
    } else {
        contextData->maxAnisoMax = 1.;
    }
#else
    assert(!createContextData); // context data is handled differently in CPU rendering
#endif

#if !defined(USE_OSMESA) && ( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) )
    if (glCreateProgram == NULL) {
        // Program
        // GL_VERSION_2_0
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
        glGetUniformfv = (PFNGLGETUNIFORMFVPROC)wglGetProcAddress("glGetUniformfv");
        glGetUniformiv = (PFNGLGETUNIFORMIVPROC)wglGetProcAddress("glGetUniformiv");
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
        glGetActiveAttrib = (PFNGLGETACTIVEATTRIBPROC)wglGetProcAddress("glGetActiveAttrib");
        glBindAttribLocation = (PFNGLBINDATTRIBLOCATIONPROC)wglGetProcAddress("glBindAttribLocation");
        glGetActiveUniform = (PFNGLGETACTIVEUNIFORMPROC)wglGetProcAddress("glGetActiveUniform");

        // Shader
        // GL_VERSION_2_0
        glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
        glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
        glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
        glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
        glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");

        // VBO
        // GL_VERSION_1_5
        glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
        glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
        glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");

        // Multitexture
        // GL_VERSION_1_3
        glActiveTexture = (PFNGLACTIVETEXTUREARBPROC)wglGetProcAddress("glActiveTexture");
        // GL_VERSION_1_3_DEPRECATED
        //glClientActiveTexture = (PFNGLCLIENTACTIVETEXTUREPROC)wglGetProcAddress("glClientActiveTexture");
        //glMultiTexCoord2f = (PFNGLMULTITEXCOORD2FPROC)wglGetProcAddress("glMultiTexCoord2f");

        // Framebuffers
        // GL_ARB_framebuffer_object
        //glIsFramebuffer = (PFNGLISFRAMEBUFFERPROC)wglGetProcAddress("glIsFramebuffer");
        glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
        glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)wglGetProcAddress("glDeleteFramebuffers");
        glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
        glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");
        glDrawBuffers = (PFNGLDRAWBUFFERSPROC)wglGetProcAddress("glDrawBuffers");
        //glFramebufferTexture1D = (PFNGLFRAMEBUFFERTEXTURE1DPROC)wglGetProcAddress("glFramebufferTexture1D");
        glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
        //glFramebufferTexture3D = (PFNGLFRAMEBUFFERTEXTURE3DPROC)wglGetProcAddress("glFramebufferTexture3D");
        //glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)wglGetProcAddress("glFramebufferRenderbuffer");
        //glGetFramebufferAttachmentParameteriv = (PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC)wglGetProcAddress("glGetFramebufferAttachmentParameteriv");
        glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)wglGetProcAddress("glGenerateMipmap");

        // GL_ARB_sync
        // Sync Objects https://www.opengl.org/wiki/Sync_Object
        glFenceSync = (PFNGLFENCESYNCPROC)wglGetProcAddress("glFenceSync​");
        glIsSync = (PFNGLISSYNCPROC)wglGetProcAddress("glIsSync");
        glDeleteSync = (PFNGLDELETESYNCPROC)wglGetProcAddress("glDeleteSync");
        glClientWaitSync = (PFNGLCLIENTWAITSYNCPROC)wglGetProcAddress("glClientWaitSync​");
        glWaitSync = (PFNGLWAITSYNCPROC)wglGetProcAddress("glWaitSync​");
    }
#endif // if !defined(USE_OSMESA) && ( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) )

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
        delete (ShadertoyShader*)( (OpenGLContextData*)contextData )->imageShader;
        ( (OpenGLContextData*)contextData )->imageShader = NULL;
        delete (OpenGLContextData*)contextData;
    } else {
        _openGLContextAttached = false;
    }
#else
    assert(!contextData); // context data is handled differently in CPU rendering
#endif
}

#ifdef USE_OSMESA
bool
ShadertoyPlugin::OSMesaDriverSelectable()
{
#ifdef OSMESA_GALLIUM_DRIVER

    return true;
#else

    return false;
#endif
}

#endif

