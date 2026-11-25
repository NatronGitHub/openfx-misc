/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
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

#ifdef OFX_SUPPORTS_OPENGLRENDER
#define USE_OPENGL
#include "TestOpenGLRender.h"

#if defined(__APPLE__)
#  include <OpenGL/OpenGL.h> // CGLGetCurrentContext
#elif defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  include <windows.h> // wglGetCurrentContext
#else
#  include <GL/glx.h> // glXGetCurrentContext
#  include <dlfcn.h> // dlsym
#endif

// Same check as Shadertoy::ShadertoyPlugin::openGLContextIsCurrent() (ShadertoyRenderGL.cpp):
// DaVinci Resolve calls the CPU render action, and we render with whatever OpenGL context is
// current, but the Fusion page has none, and GL calls without one crash.
bool
TestOpenGLPlugin::openGLContextIsCurrent()
{
#if defined(__APPLE__)
    return CGLGetCurrentContext() != nullptr;
#elif defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    return wglGetCurrentContext() != nullptr;
#else
    if (glXGetCurrentContext() != nullptr) {
        return true;
    }
    // The host may use EGL instead of GLX: look eglGetCurrentContext up at runtime,
    // so that the plugin does not depend on libEGL (it is only found if the host loaded it).
    using EGLGetCurrentContextFunc = void* (*)();
    const auto eglGetCurrentContextPtr = reinterpret_cast<EGLGetCurrentContextFunc>( dlsym(RTLD_DEFAULT, "eglGetCurrentContext") );

    return eglGetCurrentContextPtr && eglGetCurrentContextPtr() != nullptr;
#endif
}
#endif
