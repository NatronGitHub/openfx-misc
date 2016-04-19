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

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxsMultiThread.h"

#define SHADERTOY_NBINPUTS 4 // number of input channels (the standard shadertoy has 4 inputs)

void getShadertoyPluginID(OFX::PluginFactoryArray &ids);

struct ShadertoyShaderOpenGL;

/** @brief The plugin that does our work */
class ShadertoyPlugin : public OFX::ImageEffect
{
#if defined(HAVE_OSMESA)
    struct OSMesaPrivate;
#endif

public:
    /** @brief ctor */
    ShadertoyPlugin(OfxImageEffectHandle handle);

    virtual ~ShadertoyPlugin();

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* The purpose of this action is to allow a plugin to set up any data it may need
     to do OpenGL rendering in an instance. */
    virtual void contextAttached() OVERRIDE FINAL;
    /* The purpose of this action is to allow a plugin to deallocate any resource
     allocated in \ref ::kOfxActionOpenGLContextAttached just before the host
     decouples a plugin from an OpenGL context. */
    virtual void contextDetached() OVERRIDE FINAL;

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
    void contextAttachedMesa();
    void contextDetachedMesa();

    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    std::vector<OFX::Clip*> _srcClips;

    OFX::StringParam *_imageShaderFileName;
    OFX::StringParam *_imageShaderSource;
    OFX::Double2DParam *_mousePosition;
    OFX::Double2DParam *_mouseClick;
    OFX::BooleanParam *_mousePressed;
    OFX::BooleanParam *_mipmap;
    OFX::BooleanParam *_anisotropic;
    OFX::BooleanParam *_useGPUIfAvailable;

    bool _haveAniso;
    float _maxAnisoMax;
    OFX::MultiThread::Mutex _shaderMutex;
#if defined(HAVE_OSMESA)
    unsigned int _imageShaderID; // (Mesa-only) an ID that changes each time the shadertoy changes and needs to be recompiled
#endif
    ShadertoyShaderOpenGL *_imageShader; // (OpenGL-only) shader information
    bool _imageShaderChanged; // (OpenGL-only) shader ID needs to be recompiled

    OFX::MultiThread::Mutex _rendererInfoMutex;
    std::string _rendererInfoGL;

#if defined(HAVE_OSMESA)
    // A list of Mesa contexts available for rendering.
    // renderMesa() pops the last element, uses it, then pushes it back.
    // A new context is created if the list is empty.
    // That way, we can have multithreaded OSMesa rendering without having to create a context at each render
    std::list<OSMesaPrivate *> _osmesa;
    OFX::MultiThread::Mutex _osmesaMutex;

    std::string _rendererInfoMesa;
#endif
};

#endif // Misc_Shadertoy_h
