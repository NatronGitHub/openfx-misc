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

#ifndef Misc_Shadertoy_h
#define Misc_Shadertoy_h

#include <memory>
#include <climits>
#include <cfloat> // DBL_MAX

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"
#ifndef OFX_USE_MULTITHREAD_MUTEX
// some OFX hosts do not have mutex handling in the MT-Suite (e.g. Sony Catalyst Edit)
// prefer using the fast mutex by Marcus Geelnard http://tinythreadpp.bitsnbites.eu/
#include "fast_mutex.h"
#endif

#define SHADERTOY_NBINPUTS 4 // number of input channels (the standard shadertoy has 4 inputs)
#define SHADERTOY_NBUNIFORMS 20 // number of additional uniforms (if more than 7, Nuke 7's parameter page goes blank when unfolding the Extra Parameters group)
#define SHADERTOY_NBUNIFORMS_NUKE7 7

namespace Shadertoy {

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
    enum CPUDriverEnum
    {
        eCPUDriverSoftPipe = 0,
        eCPUDriverLLVMPipe,
        eCPUDriverSWR,
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

    enum FilterEnum
    {
        eFilterNearest = 0,
        eFilterLinear,
        eFilterMipmap,
        eFilterAnisotropic
    };

    enum WrapEnum
    {
        eWrapRepeat = 0,
        eWrapClamp,
        eWrapMirror
    };

    enum BBoxEnum
    {
        eBBoxDefault,
        eBBoxFormat,
        //eBBoxSize,
        eBBoxUnion,
        eBBoxIntersection,
        eBBoxIChannel
    };

    // a simple variant to represent an extra parameter
    class ExtraParameter
    {
public:
        union ExtraParameterValue
        {
            bool b; // bool
            int i; // int
            double f[4]; // float, vec2, vec3, vec4
        };

        struct less_than_pos
        {
            inline bool operator() (const ExtraParameter& struct1, const ExtraParameter& struct2)
            {
                return (struct1._pos < struct2._pos);
            }
        };
private:
        UniformTypeEnum _type;
        std::string _name;
        std::string _label;
        std::string _hint;
        ExtraParameterValue _default;
        ExtraParameterValue _min;
        ExtraParameterValue _max;
        //ExtraParameterValue _displayMin;
        //ExtraParameterValue _displayMax;
        int _pos;

public:
        ExtraParameter()
            : _type(eUniformTypeNone)
            , _pos(-1)
        {
        }

        void init(UniformTypeEnum type,
                  const std::string& name)
        {
            _type = type;
            _name = name;
            _label = name;
            _hint.clear();
            switch (type) {
            case eUniformTypeNone:
                break;
            case eUniformTypeBool:
                _default.b = false;
                break;
            case eUniformTypeInt:
                _default.i = 0;
                _min.i = INT_MIN;
                _max.i = INT_MAX;
                //_displayMin.i = INT_MIN;
                //_displayMax.i = INT_MAX;
                break;
            case eUniformTypeVec4:
                _default.f[3] = 0;
                _min.f[3] = -DBL_MAX;
                _max.f[3] = DBL_MAX;
            //_displayMin.f[3] = -DBL_MAX;
            //_displayMax.f[3] = DBL_MAX;
            case eUniformTypeVec3:
                _default.f[2] = 0;
                _min.f[2] = -DBL_MAX;
                _max.f[2] = DBL_MAX;
            //_displayMin.f[2] = -DBL_MAX;
            //_displayMax.f[2] = DBL_MAX;
            case eUniformTypeVec2:
                _default.f[1] = 0;
                _min.f[1] = -DBL_MAX;
                _max.f[1] = DBL_MAX;
            //_displayMin.f[1] = -DBL_MAX;
            //_displayMax.f[1] = DBL_MAX;
            case eUniformTypeFloat:
                _default.f[0] = 0;
                _min.f[0] = -DBL_MAX;
                _max.f[0] = DBL_MAX;
                //_displayMin.f[0] = -DBL_MAX;
                //_displayMax.f[0] = DBL_MAX;
                break;
            }
            _pos = -1;
        }

        UniformTypeEnum getType() const
        {
            return _type;
        }

        const std::string& getName() const
        {
            return _name;
        }

        const std::string& getLabel() const
        {
            return _label;
        }

        const std::string& getHint() const
        {
            return _hint;
        }

        ExtraParameterValue& getDefault()
        {
            return _default;
        }

        const ExtraParameterValue& getDefault() const
        {
            return _default;
        }

        ExtraParameterValue& getMin()
        {
            return _min;
        }

        const ExtraParameterValue& getMin() const
        {
            return _min;
        }

        ExtraParameterValue& getMax()
        {
            return _max;
        }

        const ExtraParameterValue& getMax() const
        {
            return _max;
        }

        int getPos()
        {
            return _pos;
        }

        void setPos(int pos)
        {
            _pos = pos;
        }

        void setLabel(const std::string& label)
        {
            _label = label;
        }

        void setHint(const std::string& hint)
        {
            _hint = hint;
        }

        void set(ExtraParameterValue& val,
                 bool b)
        {
            assert(_type == eUniformTypeBool);
            val.b = b;
        }

        void set(ExtraParameterValue& val,
                 int i)
        {
            assert(_type == eUniformTypeInt);
            val.i = i;
        }

        void set(ExtraParameterValue& val,
                 float f)
        {
            assert(_type == eUniformTypeFloat);
            val.f[0] = ftod(f);
        }

        void set(ExtraParameterValue& val,
                 float x,
                 float y)
        {
            assert(_type == eUniformTypeVec2);
            val.f[0] = ftod(x);
            val.f[1] = ftod(y);
        }

        void set(ExtraParameterValue& val,
                 float r,
                 float g,
                 float b)
        {
            assert(_type == eUniformTypeVec3);
            val.f[0] = ftod(r);
            val.f[1] = ftod(g);
            val.f[2] = ftod(b);
        }

        void set(ExtraParameterValue& val,
                 float r,
                 float g,
                 float b,
                 float a)
        {
            assert(_type == eUniformTypeVec4);
            val.f[0] = ftod(r);
            val.f[1] = ftod(g);
            val.f[2] = ftod(b);
            val.f[3] = ftod(a);
        }
    };

    struct Preset
    {
        Preset(const std::string& d, const std::string& f)
        : description(d)
        , filename(f)
        {
        }

        std::string description;
        std::string filename;
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
    static const char* mapUniformTypeToStr(UniformTypeEnum e)
    {
        switch (e) {
        case eUniformTypeNone:

            return NULL;
        case eUniformTypeBool:

            return "bool";
        case eUniformTypeInt:

            return "int";
        case eUniformTypeFloat:

            return "float";
        case eUniformTypeVec2:

            return "vec2";
        case eUniformTypeVec3:

            return "vec3";
        case eUniformTypeVec4:

            return "vec4";
        }

        return NULL;
    };

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

    /** @brief The sync private data action, called when the effect needs to sync any private data to persistent parameters */
    virtual void syncPrivateData(void) OVERRIDE FINAL;

private:
    void updateVisibility();
    void updateVisibilityParam(unsigned i, bool visible);
    void updateExtra();
    void updateClips();
    void resetParamsValues();
    static double ftod(float f);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    std::vector<OFX::Clip*> _srcClips;
    std::vector<OFX::BooleanParam*> _inputEnable;
    std::vector<OFX::StringParam*> _inputLabel;
    std::vector<OFX::StringParam*> _inputHint;
    std::vector<OFX::ChoiceParam*> _inputFilter;
    std::vector<OFX::ChoiceParam*> _inputWrap;
    OFX::ChoiceParam *_bbox;
    OFX::ChoiceParam *_format;
    OFX::Int2DParam *_formatSize;
    OFX::DoubleParam *_formatPar;
    OFX::StringParam *_imageShaderFileName;
    OFX::StringParam *_imageShaderPresetDir;
    OFX::ChoiceParam *_imageShaderPreset;
    OFX::StringParam *_imageShaderSource;
    OFX::PushButtonParam *_imageShaderCompile;
    OFX::IntParam *_imageShaderTriggerRender;
    OFX::BooleanParam *_imageShaderParamsUpdated;
    OFX::BooleanParam *_mouseParams;
    OFX::Double2DParam *_mousePosition;
    OFX::Double2DParam *_mouseClick;
    OFX::BooleanParam *_mousePressed;
    OFX::RGBAParam *_date;
    OFX::GroupParam *_groupExtra;
    OFX::IntParam *_paramCount;
    std::vector<OFX::GroupParam *> _paramGroup;
    std::vector<OFX::ChoiceParam *> _paramType;
    std::vector<OFX::StringParam *> _paramName;
    std::vector<OFX::StringParam *> _paramLabel;
    std::vector<OFX::StringParam *> _paramHint;
    std::vector<OFX::BooleanParam *> _paramValueBool;
    std::vector<OFX::IntParam *> _paramValueInt;
    std::vector<OFX::DoubleParam *> _paramValueFloat;
    std::vector<OFX::Double2DParam *> _paramValueVec2;
    std::vector<OFX::RGBParam *> _paramValueVec3;
    std::vector<OFX::RGBAParam *> _paramValueVec4;
    std::vector<OFX::BooleanParam *> _paramDefaultBool;
    std::vector<OFX::IntParam *> _paramDefaultInt;
    std::vector<OFX::DoubleParam *> _paramDefaultFloat;
    std::vector<OFX::Double2DParam *> _paramDefaultVec2;
    std::vector<OFX::RGBParam *> _paramDefaultVec3;
    std::vector<OFX::RGBAParam *> _paramDefaultVec4;
    std::vector<OFX::IntParam *> _paramMinInt;
    std::vector<OFX::DoubleParam *> _paramMinFloat;
    std::vector<OFX::Double2DParam *> _paramMinVec2;
    std::vector<OFX::IntParam *> _paramMaxInt;
    std::vector<OFX::DoubleParam *> _paramMaxFloat;
    std::vector<OFX::Double2DParam *> _paramMaxVec2;
    OFX::BooleanParam *_enableGPU;
    OFX::ChoiceParam *_cpuDriver;
    OFX::auto_ptr<Mutex> _imageShaderMutex;
    unsigned int _imageShaderID; // an ID that changes each time the shadertoy changes and needs to be recompiled
    unsigned int _imageShaderUniformsID; // an ID that changes each time the uniform names or count changed
    bool _imageShaderUpdateParams; // ask to extract parameters from the shader on next compilation
    std::vector<ExtraParameter> _imageShaderExtraParameters; // parameters extracted from the shader
    bool _imageShaderHasMouse; // parameters extracted from the shader
    std::vector<bool> _imageShaderInputEnabled;
    std::vector<std::string> _imageShaderInputLabel;
    std::vector<std::string> _imageShaderInputHint;
    std::vector<FilterEnum> _imageShaderInputFilter;
    std::vector<WrapEnum> _imageShaderInputWrap;
    BBoxEnum _imageShaderBBox;
    bool _imageShaderCompiled;
    struct OpenGLContextData
    {
        OpenGLContextData()
            : haveAniso(false)
            , maxAnisoMax(1.)
            , imageShader(NULL)
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
    OFX::auto_ptr<Mutex> _rendererInfoMutex;
    std::string _rendererInfo;

    std::vector<Preset> _presets;
    
#if defined(HAVE_OSMESA)
    // A list of Mesa contexts available for rendering.
    // renderMesa() pops the last element, uses it, then pushes it back.
    // A new context is created if the list is empty.
    // That way, we can have multithreaded OSMesa rendering without having to create a context at each render
    std::list<OSMesaPrivate *> _osmesa;
    OFX::auto_ptr<Mutex> _osmesaMutex;
#endif
};

void getChannelInfo(const char* fragmentShader, unsigned channel, std::string& label, std::string& hint, ShadertoyPlugin::FilterEnum& filter, ShadertoyPlugin::WrapEnum& wrap);

void getBboxInfo(const char* fragmentShader, ShadertoyPlugin::BBoxEnum& bbox);

void getExtraParameterInfo(const char* fragmentShader, ShadertoyPlugin::ExtraParameter &p);

} // namespace Shadertoy

#endif // Misc_Shadertoy_h
