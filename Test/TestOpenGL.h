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
 * OFX TestOpenGL plugin.
 */

#ifndef Misc_TestOpenGL_h
#define Misc_TestOpenGL_h

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"
#ifndef OFX_USE_MULTITHREAD_MUTEX
// some OFX hosts do not have mutex handling in the MT-Suite (e.g. Sony Catalyst Edit)
// prefer using the fast mutex by Marcus Geelnard http://tinythreadpp.bitsnbites.eu/
#include "fast_mutex.h"
#endif

void getTestOpenGLPluginID(OFX::PluginFactoryArray &ids);

/** @brief The plugin that does our work */
class TestOpenGLPlugin
    : public OFX::ImageEffect
{
#if defined(HAVE_OSMESA)
    struct OSMesaPrivate;
#endif

public:
#if defined(HAVE_OSMESA)
    enum CPUDriverEnum
    {
        eCPUDriverSoftPipe = 0,
        eCPUDriverLLVMPipe,
        eCPUDriverSWR,
    };

#endif

#ifdef OFX_USE_MULTITHREAD_MUTEX
    typedef OFX::MultiThread::Mutex Mutex;
    typedef OFX::MultiThread::AutoMutex AutoMutex;
#else
    typedef tthread::fast_mutex Mutex;
    typedef OFX::MultiThread::AutoMutexT<tthread::fast_mutex> AutoMutex;
#endif

public:
    /** @brief ctor */
    TestOpenGLPlugin(OfxImageEffectHandle handle);

    virtual ~TestOpenGLPlugin();

#ifdef HAVE_OSMESA
    static bool OSMesaDriverSelectable();
#endif

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

#ifdef OFX_SUPPORTS_OPENGLRENDER
    /* The purpose of this action is to allow a plugin to set up any data it may need
       to do OpenGL rendering in an instance. */
    virtual void* contextAttached(bool createContextData) OVERRIDE FINAL;
    /* The purpose of this action is to allow a plugin to deallocate any resource
       allocated in \ref ::kOfxActionOpenGLContextAttached just before the host
       decouples a plugin from an OpenGL context. */
    virtual void contextDetached(void* contextData) OVERRIDE FINAL;
#endif

    /* The OpenGL context is also set when beginSequenceRender() and endSequenceRender()
       are called. This may be useful to allocate/deallocate sequence-specific OpenGL data. */
    //virtual void beginSequenceRender(const OFX::BeginSequenceRenderArguments &args) OVERRIDE FINAL;
    //virtual void endSequenceRender(const OFX::EndSequenceRenderArguments &args) OVERRIDE FINAL;

    void initMesa();
    void exitMesa();
    void renderGL(const OFX::RenderArguments &args);
    void renderMesa(const OFX::RenderArguments &args);
    void* contextAttachedMesa(bool createContextData);
    void contextDetachedMesa(void* contextData);

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;
    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Double2DParam *_scale;
    OFX::Double2DParam *_sourceScale;
    OFX::DoubleParam *_sourceStretch;
    OFX::DoubleParam *_teapotScale;
    OFX::DoubleParam *_angleX;
    OFX::DoubleParam *_angleY;
    OFX::DoubleParam *_angleZ;
    OFX::BooleanParam *_projective;
    OFX::BooleanParam *_mipmap;
    OFX::BooleanParam *_anisotropic;
    OFX::BooleanParam *_enableGPU;
    OFX::ChoiceParam *_cpuDriver;
    struct OpenGLContextData
    {
        OpenGLContextData()
            : haveAniso(false)
            , maxAnisoMax(1.)
        {
        }

        bool haveAniso;
        float maxAnisoMax;
    };

    OpenGLContextData _openGLContextData; // (OpenGL-only) - the single openGL context, in case the host does not support kNatronOfxImageEffectPropOpenGLContextData
    bool _openGLContextAttached; // (OpenGL-only) - set to true when the contextAttached function is executed - used for checking non-conformant hosts such as Sony Catalyst
    OFX::auto_ptr<Mutex> _rendererInfoMutex;
    std::string _rendererInfo;

#if defined(HAVE_OSMESA)
    // A list of Mesa contexts available for rendering.
    // renderMesa() pops the last element, uses it, then pushes it back.
    // A new context is created if the list is empty.
    // That way, we can have multithreaded OSMesa rendering without having to create a context at each render
    std::list<OSMesaPrivate *> _osmesa;
    OFX::auto_ptr<Mutex> _osmesaMutex;
#endif
};

#endif // Misc_TestOpenGL_h
