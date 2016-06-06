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

#ifndef Misc_Shadertoy_h
#define Misc_Shadertoy_h

#include <memory>

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxsMultiThread.h"
#ifndef OFX_USE_MULTITHREAD_MUTEX
// some OFX hosts do not have mutex handling in the MT-Suite (e.g. Sony Catalyst Edit)
// prefer using the fast mutex by Marcus Geelnard http://tinythreadpp.bitsnbites.eu/
#include "fast_mutex.h"
#endif

#define SHADERTOY_NBINPUTS 4 // number of input channels (the standard shadertoy has 4 inputs)
#define SHADERTOY_NBUNIFORMS 10 // number of additional uniforms

void getShadertoyPluginID(OFX::PluginFactoryArray &ids);

struct ShadertoyShaderOpenGL;

/** @brief The plugin that does our work */
class ShadertoyPlugin
    : public OFX::ImageEffect
{
#if defined(HAVE_OSMESA)
    struct OSMesaPrivate;
#endif

public:
#if defined(HAVE_OSMESA)
    enum CPUDriverEnum {
        eCPUDriverSoftPipe = 0,
        eCPUDriverLLVMPipe
    };
#endif

    enum UniformTypeEnum
    {
        eUniformTypeNone,
        eUniformTypeBool,
        eUniformTypeInt,
        eUniformTypeFloat,
        eUniformTypeVec2,
        eUniformTypeVec3,
        eUniformTypeVec4,
    };

#ifdef OFX_USE_MULTITHREAD_MUTEX
    typedef OFX::MultiThread::Mutex Mutex;
    typedef OFX::MultiThread::AutoMutex AutoMutex;
#else
    typedef tthread::fast_mutex Mutex;
    typedef OFX::MultiThread::AutoMutexT<tthread::fast_mutex> AutoMutex;
#endif

public:
    /** @brief ctor */
    ShadertoyPlugin(OfxImageEffectHandle handle);

    virtual ~ShadertoyPlugin();

#ifdef HAVE_OSMESA
    static bool OSMesaDriverSelectable();
#endif

public:
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* The purpose of this action is to allow a plugin to set up any data it may need
       to do OpenGL rendering in an instance. */
    virtual void* contextAttached(bool createContextData) OVERRIDE FINAL;
    /* The purpose of this action is to allow a plugin to deallocate any resource
       allocated in \ref ::kOfxActionOpenGLContextAttached just before the host
       decouples a plugin from an OpenGL context. */
    virtual void contextDetached(void* contextData) OVERRIDE FINAL;

    /* The OpenGL context is also set when beginSequenceRender() and endSequenceRender()
       are called. This may be useful to allocate/deallocate sequence-specific OpenGL data. */
    //virtual void beginSequenceRender(const OFX::BeginSequenceRenderArguments &args) OVERRIDE FINAL;
    //virtual void endSequenceRender(const OFX::EndSequenceRenderArguments &args) OVERRIDE FINAL;

    void initOpenGL();
    void initMesa();
    void exitOpenGL();
    void exitMesa();
    void renderGL(const OFX::RenderArguments &args);
    void renderMesa(const OFX::RenderArguments &args);
    void* contextAttachedMesa(bool createContextData);
    void contextDetachedMesa(void* contextData);

    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:
    void updateVisibility();
    void updateVisibilityParam(unsigned i, bool visible);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    std::vector<OFX::Clip*> _srcClips;
    OFX::ChoiceParam *_bbox;
    OFX::ChoiceParam *_format;
    OFX::Int2DParam *_formatSize;
    OFX::DoubleParam *_formatPar;
    OFX::StringParam *_imageShaderFileName;
    OFX::StringParam *_imageShaderSource;
    OFX::PushButtonParam *_imageShaderCompile;
    OFX::IntParam *_imageShaderTriggerRender;
    OFX::Double2DParam *_mousePosition;
    OFX::Double2DParam *_mouseClick;
    OFX::BooleanParam *_mousePressed;
    OFX::IntParam *_paramCount;
    std::vector<OFX::ChoiceParam *> _paramType;
    std::vector<OFX::StringParam *> _paramName;
    std::vector<OFX::BooleanParam *> _paramValueBool;
    std::vector<OFX::IntParam *> _paramValueInt;
    std::vector<OFX::DoubleParam *> _paramValueFloat;
    std::vector<OFX::Double2DParam *> _paramValueVec2;
    std::vector<OFX::Double3DParam *> _paramValueVec3;
    std::vector<OFX::RGBAParam *> _paramValueVec4;
    OFX::BooleanParam *_mipmap;
    OFX::BooleanParam *_anisotropic;
    OFX::BooleanParam *_enableGPU;
    OFX::ChoiceParam *_cpuDriver;
    std::auto_ptr<Mutex> _shaderMutex;
    unsigned int _imageShaderID; // an ID that changes each time the shadertoy changes and needs to be recompiled
    unsigned int _imageShaderUniformsID; // an ID that changes each time the uniform names or count changed
    struct OpenGLContextData
    {
        OpenGLContextData()
            : haveAniso(false)
            , maxAnisoMax(1.)
            , imageShader(0)
            , imageShaderID(0)
            , imageShaderUniformsID(0)
        {
        }

        bool haveAniso;
        float maxAnisoMax;
        void *imageShader; //shader information
        unsigned int imageShaderID; // the shader ID compiled for this context
        unsigned int imageShaderUniformsID; // the ID for custom uniform locations
    };

    OpenGLContextData _openGLContextData; // (OpenGL-only) - the single openGL context, in case the host does not support kNatronOfxImageEffectPropOpenGLContextData
    bool _openGLContextAttached; // (OpenGL-only) - set to true when the contextAttached function is executed - used for checking non-conformant hosts such as Sony Catalyst
    std::auto_ptr<Mutex> _rendererInfoMutex;
    std::string _rendererInfoGL;

#if defined(HAVE_OSMESA)
    // A list of Mesa contexts available for rendering.
    // renderMesa() pops the last element, uses it, then pushes it back.
    // A new context is created if the list is empty.
    // That way, we can have multithreaded OSMesa rendering without having to create a context at each render
    std::list<OSMesaPrivate *> _osmesa;
    std::auto_ptr<Mutex> _osmesaMutex;
    std::string _rendererInfoMesa[2];
#endif
};

#endif // Misc_Shadertoy_h
