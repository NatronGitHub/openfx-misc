/*
 OFX ColorCorrect plugin.
 
 Copyright (C) 2014 INRIA
 
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
 
 
 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.
 
 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England
 
 */

#include "ColorCorrect.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "../include/ofxsProcessing.H"


////std strings because we need them in changedParam
static const std::string kColorCorrectMasterGroupName = std::string("Master");
static const std::string kColorCorrectShadowsGroupName = std::string("Shadows");
static const std::string kColorCorrectMidtonesGroupName = std::string("Midtones");
static const std::string kColorCorrectHighlightsGroupName = std::string("Highlights");

static const std::string kColorCorrectSaturationName = std::string("Saturation");
static const std::string kColorCorrectContrastName = std::string("Contrast");
static const std::string kColorCorrectGammaName = std::string("Gamma");
static const std::string kColorCorrectGainName = std::string("Gain");
static const std::string kColorCorrectOffsetName = std::string("Offset");

#define kColorCorrectToneRangesParamName "ToneRanges"
class ColorCorrecterBase : public OFX::ImageProcessor
{
    
public:
    
    struct ColorControlValues { double red,green,blue,alpha; };
    
    struct ColorControlGroup { ColorControlValues saturation,contrast,gamma,gain,offset; };
    
protected :
    
    OFX::Image *_srcImg;
    OFX::Image *_maskImg;
    bool   _doMasking;
    ColorControlGroup _masterValues;
    ColorControlGroup _shadowValues;
    ColorControlGroup _midtoneValues;
    ColorControlGroup _highlightsValues;

    
public :
    
    ColorCorrecterBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    ,_doMasking(false)
    {
    }
    
    void setSrcImg(OFX::Image *v) {_srcImg = v;}
    
    void setMaskImg(OFX::Image *v) {_maskImg = v;}
    
    void doMasking(bool v) {_doMasking = v;}
    
    void setColorControlValues(const ColorControlGroup& master,
                               const ColorControlGroup& shadow,
                               const ColorControlGroup& midtone,
                               const ColorControlGroup& hightlights)
    {
        _masterValues = master;
        _shadowValues = shadow;
        _midtoneValues = midtone;
        _highlightsValues = hightlights;
    }
};

template <class PIX, int nComponents, int max>
class ColorCorrecter : public ColorCorrecterBase
{
    public :
    ColorCorrecter(OFX::ImageEffect &instance): ColorCorrecterBase(instance)
    {}
    void multiThreadProcessImages(OfxRectI procWindow)
    {
//        float scales[4];
//        scales[0] = nComponents == 1 ? (float)_aScale : (float)_rScale;
//        scales[1] = (float)_gScale;
//        scales[2] = (float)_bScale;
//        scales[3] = (float)_aScale;
//        float maskScale = 1.0f;
//        for(int y = procWindow.y1; y < procWindow.y2; y++)
//        {
//            if(_effect.abort())
//                break;
//            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
//            for(int x = procWindow.x1; x < procWindow.x2; x++)
//            {
//                PIX *srcPix = (PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
//                if(_doMasking)
//                {
//                    if(!_maskImg)
//                        maskScale = 1.0f;
//                    else
//                    {
//                        PIX *maskPix = (PIX *)  (_maskImg ? _maskImg->getPixelAddress(x, y) : 0);
//                        maskScale = maskPix != 0 ? float(*maskPix)/float(max) : 0.0f;
//                    }
//                }
//                if(srcPix)
//                {
//                    for(int c = 0; c < nComponents; c++)
//                    {
//                        float v = (float)(pow((double)srcPix[c], (double)scales[c])) * maskScale + (1.0f - maskScale) * srcPix[c];
//                        if(max == 1)
//                            dstPix[c] = PIX(v);
//                        else
//                            dstPix[c] = PIX(Clamp(v, 0, max));
//                    }
//                }
//                else
//                {
//                    for(int c = 0; c < nComponents; c++)
//                        dstPix[c] = 0;
//                }
//                dstPix += nComponents;
//            }
//        }
    }
};

namespace  {
    
    struct ColorControl {
        OFX::BooleanParam* perComp;
        OFX::DoubleParam* master;
        OFX::DoubleParam* red;
        OFX::DoubleParam* green;
        OFX::DoubleParam* blue;
        OFX::DoubleParam* alpha;
    };
    
    struct ColorControlGroup {
        ColorControl saturation;
        ColorControl contrast;
        ColorControl gamma;
        ColorControl gain;
        ColorControl offset;
    };
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ColorCorrectPlugin : public OFX::ImageEffect {
    
    void fetchColorControl(const std::string& controlName,ColorControl* control) {
        control->perComp = fetchBooleanParam(controlName + ".scaleComps");
        control->master = fetchDoubleParam(controlName + ".master");
        control->red = fetchDoubleParam(controlName + ".r");
        control->green = fetchDoubleParam(controlName + ".g");
        control->blue = fetchDoubleParam(controlName + ".b");
        control->alpha = fetchDoubleParam(controlName + ".a");
    }
    
    void fetchColorControlGroup(const std::string& groupName,ColorControlGroup* group) {
        fetchColorControl(groupName + '.' + kColorCorrectSaturationName,&group->saturation);
        fetchColorControl(groupName + '.' + kColorCorrectContrastName,&group->contrast);
        fetchColorControl(groupName + '.' + kColorCorrectGammaName,&group->gamma);
        fetchColorControl(groupName + '.' + kColorCorrectGainName,&group->gain);
        fetchColorControl(groupName + '.' + kColorCorrectOffsetName,&group->offset);
    }
    
protected :
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;
    
    ColorControlGroup _masterParamsGroup;
    ColorControlGroup _shadowsParamsGroup;
    ColorControlGroup _midtonesParamsGroup;
    ColorControlGroup _highlightsParamsGroup;
    OFX::ParametricParam* _rangesParam;
    
public :
    
    enum ColorCorrectGroupType {
        eGroupMaster = 0,
        eGroupShadow,
        eGroupMidtone,
        eGroupHighlight
    };
    
    /** @brief ctor */
    ColorCorrectPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , maskClip_(0)
    
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        fetchColorControlGroup(kColorCorrectMasterGroupName, &_masterParamsGroup);
        fetchColorControlGroup(kColorCorrectShadowsGroupName, &_shadowsParamsGroup);
        fetchColorControlGroup(kColorCorrectMidtonesGroupName, &_midtonesParamsGroup);
        fetchColorControlGroup(kColorCorrectHighlightsGroupName, &_highlightsParamsGroup);
        _rangesParam = fetchParametricParam(kColorCorrectToneRangesParamName);
    }
    
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);
    
    /* set up and run a processor */
    void setupAndProcess(ColorCorrecterBase &, const OFX::RenderArguments &args);
    
    void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);
    
private:
    
    void getColorCorrectGroupValues(ColorCorrecterBase::ColorControlGroup* groupValues,ColorCorrectGroupType type);

    ColorControlGroup& getGroup(ColorCorrectGroupType type) {
        switch (type) {
            case eGroupMaster:
                return _masterParamsGroup;
            case eGroupShadow:
                return _shadowsParamsGroup;
            case eGroupMidtone:
                return _midtonesParamsGroup;
            case eGroupHighlight:
                return _highlightsParamsGroup;
            default:
                assert(false);
                break;
        }
    }
};


void ColorCorrectPlugin::getColorCorrectGroupValues(ColorCorrecterBase::ColorControlGroup* groupValues,ColorCorrectGroupType type)
{
    
    ColorControlGroup& group = getGroup(type);
    group.saturation.red->getValue(groupValues->saturation.red);
    group.saturation.green->getValue(groupValues->saturation.green);
    group.saturation.blue->getValue(groupValues->saturation.blue);
    group.saturation.alpha->getValue(groupValues->saturation.alpha);
    
    group.contrast.red->getValue(groupValues->contrast.red);
    group.contrast.green->getValue(groupValues->contrast.green);
    group.contrast.blue->getValue(groupValues->contrast.blue);
    group.contrast.alpha->getValue(groupValues->contrast.alpha);
    
    group.gamma.red->getValue(groupValues->gamma.red);
    group.gamma.green->getValue(groupValues->gamma.green);
    group.gamma.blue->getValue(groupValues->gamma.blue);
    group.gamma.alpha->getValue(groupValues->gamma.alpha);
    
    group.gain.red->getValue(groupValues->gain.red);
    group.gain.green->getValue(groupValues->gain.green);
    group.gain.blue->getValue(groupValues->gain.blue);
    group.gain.alpha->getValue(groupValues->gain.alpha);
    
    group.offset.red->getValue(groupValues->offset.red);
    group.offset.green->getValue(groupValues->offset.green);
    group.offset.blue->getValue(groupValues->offset.blue);
    group.offset.alpha->getValue(groupValues->offset.alpha);
    
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
ColorCorrectPlugin::setupAndProcess(ColorCorrecterBase &processor, const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
    if(src.get())
    {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            throw int(1);
    }
    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(args.time) : 0);
    if(getContext() != OFX::eContextFilter)
    {
        processor.doMasking(true);
        processor.setMaskImg(mask.get());
    }
    
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);
    
    ColorCorrecterBase::ColorControlGroup masterValues,shadowValues,midtoneValues,highlightValues;
    getColorCorrectGroupValues(&masterValues,eGroupMaster);
    getColorCorrectGroupValues(&shadowValues,eGroupShadow);
    getColorCorrectGroupValues(&midtoneValues,eGroupMidtone);
    getColorCorrectGroupValues(&highlightValues,eGroupHighlight);
    processor.setColorControlValues(masterValues, shadowValues, midtoneValues, highlightValues);
    processor.process();
}

// the overridden render function
void
ColorCorrectPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if(dstComponents == OFX::ePixelComponentRGBA)
    {
        switch(dstBitDepth)
        {
            case OFX::eBitDepthUByte :
            {
                ColorCorrecter<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                ColorCorrecter<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                ColorCorrecter<float, 4, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
    else
    {
        switch(dstBitDepth)
        {
            case OFX::eBitDepthUByte :
            {
                ColorCorrecter<unsigned char, 1, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                ColorCorrecter<unsigned short, 1, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                ColorCorrecter<float, 1, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

static bool stringEndsWith(const std::string& fullString,const std::string& ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.size() - ending.size(), ending.size(), ending));
    } else {
        return false;
    }
}

void ColorCorrectPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    
    
    if (stringEndsWith(paramName, ".scaleComps")) {
        ColorCorrectGroupType groupType;
        std::string groupName;
        if (paramName.compare(0, kColorCorrectMasterGroupName.size() + 1, kColorCorrectMasterGroupName + '.') == 0) {
            groupType = eGroupMaster;
            groupName = kColorCorrectMasterGroupName;
        } else if (paramName.compare(0, kColorCorrectShadowsGroupName.size() + 1, kColorCorrectShadowsGroupName + '.') == 0) {
            groupType = eGroupShadow;
            groupName = kColorCorrectShadowsGroupName;
        } else if (paramName.compare(0, kColorCorrectMidtonesGroupName.size() + 1, kColorCorrectMidtonesGroupName + '.') == 0) {
            groupType = eGroupMidtone;
            groupName = kColorCorrectMidtonesGroupName;
        } else if (paramName.compare(0, kColorCorrectHighlightsGroupName.size() + 1, kColorCorrectHighlightsGroupName + '.') == 0) {
            groupType = eGroupHighlight;
            groupName = kColorCorrectHighlightsGroupName;
        } else {
            assert(false);
        }
        
        assert(!groupName.empty());
        std::string paramNameGroupCut = paramName;
        paramNameGroupCut.erase(0,groupName.size() + 1);
        ColorControlGroup& group = getGroup(groupType);
        if (paramNameGroupCut.compare(0, kColorCorrectSaturationName.size() + 1, kColorCorrectSaturationName + '.') == 0) {
            bool enablePerComps;
            group.saturation.perComp->getValue(enablePerComps);
            group.saturation.master->setIsSecret(enablePerComps);
            group.saturation.red->setIsSecret(!enablePerComps);
            group.saturation.green->setIsSecret(!enablePerComps);
            group.saturation.blue->setIsSecret(!enablePerComps);
            group.saturation.alpha->setIsSecret(!enablePerComps);
            
        } else if (paramNameGroupCut.compare(0, kColorCorrectContrastName.size() + 1, kColorCorrectContrastName + '.') == 0) {
            bool enablePerComps;
            group.contrast.perComp->getValue(enablePerComps);
            group.contrast.master->setIsSecret(enablePerComps);
            group.contrast.red->setIsSecret(!enablePerComps);
            group.contrast.green->setIsSecret(!enablePerComps);
            group.contrast.blue->setIsSecret(!enablePerComps);
            group.contrast.alpha->setIsSecret(!enablePerComps);
        } else if (paramNameGroupCut.compare(0, kColorCorrectGammaName.size() + 1, kColorCorrectGammaName + '.') == 0) {
            bool enablePerComps;
            group.gamma.perComp->getValue(enablePerComps);
            group.gamma.master->setIsSecret(enablePerComps);
            group.gamma.red->setIsSecret(!enablePerComps);
            group.gamma.green->setIsSecret(!enablePerComps);
            group.gamma.blue->setIsSecret(!enablePerComps);
            group.gamma.alpha->setIsSecret(!enablePerComps);
        } else if (paramNameGroupCut.compare(0, kColorCorrectGainName.size() + 1, kColorCorrectGainName + '.') == 0) {
            bool enablePerComps;
            group.gain.perComp->getValue(enablePerComps);
            group.gain.master->setIsSecret(enablePerComps);
            group.gain.red->setIsSecret(!enablePerComps);
            group.gain.green->setIsSecret(!enablePerComps);
            group.gain.blue->setIsSecret(!enablePerComps);
            group.gain.alpha->setIsSecret(!enablePerComps);
        } else if (paramNameGroupCut.compare(0, kColorCorrectOffsetName.size() + 1, kColorCorrectOffsetName + '.') == 0) {
            bool enablePerComps;
            group.offset.perComp->getValue(enablePerComps);
            group.offset.master->setIsSecret(enablePerComps);
            group.offset.red->setIsSecret(!enablePerComps);
            group.offset.green->setIsSecret(!enablePerComps);
            group.offset.blue->setIsSecret(!enablePerComps);
            group.offset.alpha->setIsSecret(!enablePerComps);
        }

    }
    
    
}

using namespace OFX;
void ColorCorrectPluginFactory::load()
{
    
}

void ColorCorrectPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("ColorCorrect", "ColorCorrect", "ColorCorrect");
    desc.setPluginGrouping("Color");
    desc.setPluginDescription("");
    
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);
    
    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(true);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    
}

DoubleParamDescriptor *defineScaleParam(OFX::ImageEffectDescriptor &desc,
                                        const std::string &name, const std::string &label, const std::string &hint,
                                        GroupParamDescriptor *parent, double def,double min,double max)
{
    DoubleParamDescriptor *param = desc.defineDoubleParam(name);
    param->setLabels(label, label, label);
    param->setScriptName(name);
    param->setHint(hint);
    param->setDefault(def);
    param->setRange(min, max);
    param->setIncrement(0.1);
    param->setDisplayRange(min, max);
    param->setDoubleType(eDoubleTypeScale);
    if(parent)
        param->setParent(*parent);
    return param;
}

void defineRGBAScaleParam(OFX::ImageEffectDescriptor &desc,
                          const std::string &name, const std::string &label, const std::string &hint,
                          GroupParamDescriptor *parent, PageParamDescriptor* page,double def , double min,double max)
{
    
    GroupParamDescriptor* group = desc.defineGroupParam(name + ".group");
    group->setLabels(label, label, label);
    if (parent) {
        group->setParent(*parent);
    }
    
    BooleanParamDescriptor* scaleComps = desc.defineBooleanParam(name + '.' + "scaleComps");
    scaleComps->setLabels("Per component", "Per component", "Per component");
    scaleComps->setHint("Enables per component adjustements.");
    scaleComps->setDefault(false);
    scaleComps->setParent(*group);
    scaleComps->setAnimates(false);
    page->addChild(*scaleComps);
    
    DoubleParamDescriptor* master = defineScaleParam(desc, name + '.' + "master" , "Master", hint, group, def, min, max);
    page->addChild(*master);
    
    DoubleParamDescriptor* red = defineScaleParam(desc, name + '.' + "r" , "r", hint + " Controls the red channel.", group, def, min, max);
    red->setLayoutHint(OFX::eLayoutHintNoNewLine);
    red->setIsSecret(true);
    page->addChild(*red);
    
    DoubleParamDescriptor* green = defineScaleParam(desc, name + '.' + "g" , "g", hint+ " Controls the green channel.", group, def, min, max);
    green->setLayoutHint(OFX::eLayoutHintNoNewLine);
    green->setIsSecret(true);
    page->addChild(*green);
    
    DoubleParamDescriptor* blue = defineScaleParam(desc, name + '.' + "b" , "b", hint+ " Controls the blue channel.", group, def, min, max);
    blue->setLayoutHint(OFX::eLayoutHintNoNewLine);
    blue->setIsSecret(true);
    page->addChild(*blue);
    
    DoubleParamDescriptor* alpha = defineScaleParam(desc, name + '.' + "a" , "a", hint+ " Controls the alpha channel.", group, def, min, max);
    alpha->setIsSecret(true);
    page->addChild(*alpha);
    
}

void defineColorGroup(const std::string& groupName,const std::string& hint,PageParamDescriptor* page,OFX::ImageEffectDescriptor &desc,
                      bool makeNewTab) {
    GroupParamDescriptor* groupDesc = 0;
    if (makeNewTab) {
        groupDesc = desc.defineGroupParam(groupName);
        groupDesc->setLabels(groupName, groupName, groupName);
        groupDesc->setHint(hint);
        groupDesc->setAsTab();
    }
    
    defineRGBAScaleParam(desc, groupName + '.' + kColorCorrectSaturationName, kColorCorrectSaturationName, hint, groupDesc, page, 1, 0, 4);
    defineRGBAScaleParam(desc, groupName + '.' + kColorCorrectContrastName, kColorCorrectContrastName, hint, groupDesc, page, 1, 0, 4);
    defineRGBAScaleParam(desc, groupName + '.' + kColorCorrectGammaName, kColorCorrectGammaName, hint, groupDesc, page, 1, 0, 10);
    defineRGBAScaleParam(desc, groupName + '.' + kColorCorrectGainName, kColorCorrectGainName, hint, groupDesc, page, 1, 0, 10);
    defineRGBAScaleParam(desc, groupName + '.' + kColorCorrectOffsetName, kColorCorrectOffsetName, hint, groupDesc, page, 0, -1, 1);

}

void ColorCorrectPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    if (!OFX::getImageEffectHostDescription()->supportsParametricParameter) {
        throwHostMissingSuiteException(kOfxParametricParameterSuite);
    }

    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(true);
    
    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if(context == eContextGeneral)
            maskClip->setOptional(true);
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true);
    }
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    defineColorGroup(kColorCorrectMasterGroupName,"", page, desc,false);
    defineColorGroup(kColorCorrectShadowsGroupName,"" ,page, desc,true);
    defineColorGroup(kColorCorrectMidtonesGroupName, "",page, desc,true);
    defineColorGroup(kColorCorrectHighlightsGroupName, "",page, desc,true);
    
    GroupParamDescriptor* curvesTab = desc.defineGroupParam("Ranges");
    curvesTab->setLabels("Ranges", "Ranges", "Ranges");
    curvesTab->setAsTab();
    
    OFX::ParametricParamDescriptor* lookupTable = desc.defineParametricParam(kColorCorrectToneRangesParamName);
    assert(lookupTable);
    lookupTable->setLabel(kColorCorrectToneRangesParamName);
    lookupTable->setScriptName(kColorCorrectToneRangesParamName);
    
    // define it as three dimensional
    lookupTable->setDimension(2);
    
    lookupTable->setDimensionLabel("Shadow", 0);
    lookupTable->setDimensionLabel("Highlight", 1);
    
    // set the UI colour for each dimension
    const OfxRGBColourD shadow   = {0.6,0.4,0.6};
    const OfxRGBColourD highlight  =  {0.8,0.7,0.6};
    lookupTable->setUIColour( 0, shadow );
    lookupTable->setUIColour( 1, highlight );
    
    // set the min/max parametric range to 0..1
    lookupTable->setRange(0.0, 1.0);
    
    lookupTable->addControlPoint(0, // curve to set
                                 0.0,   // time, ignored in this case, as we are not adding a key
                                 0.0,   // parametric position, zero
                                 1.0,   // value to be, 0
                                 false);   // don't add a key
    lookupTable->addControlPoint(0, 0.0, 0.09, 0.0,false);
    
    lookupTable->addControlPoint(1, 0.0, 0.5, 0.0,false);
    lookupTable->addControlPoint(1, 0.0, 1.0, 1.0,false);
    
    lookupTable->setParent(*curvesTab);
    page->addChild(*lookupTable);

    
}

OFX::ImageEffect* ColorCorrectPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new ColorCorrectPlugin(handle);
}

