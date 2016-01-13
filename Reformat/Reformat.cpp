/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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
 * OFX Transform & DirBlur plugins.
 */

#include <cmath>
#include <iostream>
#include <limits>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsTransform3x3.h"
#include "ofxsTransformInteract.h"
#include "ofxsFormatResolution.h"
#include "ofxsCoords.h"

#define kPluginName "ReformatOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription "Convert the image to another format or size\n"\
"This plugin concatenates transforms."
#define kPluginIdentifier "net.sf.openfx.Reformat"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kParamType "reformatType"
#define kParamTypeLabel "Type"
#define kParamTypeHint "Format: Converts between formats, the image is resized to fit in the target format. " \
"Size: Scales to fit into a box of a given width and height. " \
"Scale: Scales the image."
#define kParamTypeOptionFormat "Format"
#define kParamTypeOptionSize "Size"
#define kParamTypeOptionScale "Scale"

enum ReformatTypeEnum
{
    eReformatTypeFormat = 0,
    eReformatTypeSize,
    eReformatTypeScale,
};

#define kParamFormat "format"
#define kParamFormatLabel "Format"
#define kParamFormatHint "The output format"

#define kParamSize "reformatSize"
#define kParamSizeLabel "Size"
#define kParamSizeHint "The output size"

#define kParamPreservePAR "preservePAR"
#define kParamPreservePARLabel "Preserve PAR"
#define kParamPreservePARHint "Preserve Pixel Aspect Ratio (PAR). When checked, one direction will be clipped."

#define kParamScale "reformatScale"
#define kParamScaleLabel "Scale"
#define kParamScaleHint "The scale factor to apply to the image."

#define kParamScaleUniform "reformatScaleUniform"
#define kParamScaleUniformLabel "Uniform"
#define kParamScaleUniformHint "Use the X scale for both directions"

#define kParamReformatCenter "reformatCentered"
#define kParamReformatCenterLabel "Center"
#define kParamReformatCenterHint "Center the image before applying the transformation, otherwise it will be transformed around it's lower-left corner."

#define kParamFlip "flip"
#define kParamFlipLabel "Flip"
#define kParamFlipHint "Mirror the image vertically"

#define kParamFlop "flop"
#define kParamFlopLabel "Flop"
#define kParamFlopHint "Mirror the image horizontally"

#define kParamTurn "turn"
#define kParamTurnLabel "Turn"
#define kParamTurnHint "Rotate the image by 90 degrees"

#define kSrcClipChanged "srcClipChanged"

#define kParamDisableConcat "disableConcat"
#define kParamDisableConcatLabel "Disable Concatenation"
#define kParamDisableConcatHint "Disable the transform concatenation for this node to force it to be rendered.\n" \
"This can be useful for example when connecting a Reformat node to a Read node and then connecting the Reformat to a STMap node: " \
"Since the STMap concatenates transforms, it would just pick the pixels from the Read node directly instead of the resized image, being " \
"in this case much slower."

using namespace OFX;


static bool gHostCanTransform;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ReformatPlugin : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    ReformatPlugin(OfxImageEffectHandle handle)
    : Transform3x3Plugin(handle, false, eTransform3x3ParamsTypeNone)
    , _type(0)
    , _format(0)
    , _size(0)
    , _scale(0)
    , _scaleUniform(0)
    , _preservePAR(0)
    , _srcClipChanged(0)
    , _disableConcat(0)
    , _centered(0)
    , _flip(0)
    , _flop(0)
    , _turn(0)
    {
        
        _filter = fetchChoiceParam(kParamFilterType);
        _clamp = fetchBooleanParam(kParamFilterClamp);
        _blackOutside = fetchBooleanParam(kParamFilterBlackOutside);
        
        // NON-GENERIC
        _type = fetchChoiceParam(kParamType);
        _format = fetchChoiceParam(kParamFormat);
        _size = fetchInt2DParam(kParamSize);
        _scale = fetchDouble2DParam(kParamScale);
        _scaleUniform = fetchBooleanParam(kParamScaleUniform);
        _preservePAR = fetchBooleanParam(kParamPreservePAR);
        _srcClipChanged = fetchBooleanParam(kSrcClipChanged);
        if (gHostCanTransform) {
            _disableConcat = fetchBooleanParam(kParamDisableConcat);
        }
        _centered = fetchBooleanParam(kParamReformatCenter);
        _flip = fetchBooleanParam(kParamFlip);
        _flop = fetchBooleanParam(kParamFlop);
        _turn = fetchBooleanParam(kParamTurn);
        assert(_type && _format && _size && _scale && _scaleUniform && _preservePAR && _srcClipChanged && _flip && _flop && _turn && _centered);
        
        refreshVisibility();
        refreshDynamicProps();
    }

private:
    virtual bool isIdentity(double time) OVERRIDE FINAL;

    virtual bool getInverseTransformCanonical(double time, int view, double amount, bool invert, OFX::Matrix3x3* invtransform) const OVERRIDE FINAL;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    
    void refreshVisibility();
    
    void refreshDynamicProps();
    
    // NON-GENERIC
    OFX::ChoiceParam *_type;
    OFX::ChoiceParam *_format;
    OFX::Int2DParam *_size;
    OFX::Double2DParam *_scale;
    OFX::BooleanParam* _scaleUniform;
    OFX::BooleanParam *_preservePAR;
    OFX::BooleanParam* _srcClipChanged; // set to true the first time the user connects src
    OFX::BooleanParam* _disableConcat;
    OFX::BooleanParam* _centered;
    OFX::BooleanParam* _flip;
    OFX::BooleanParam* _flop;
    OFX::BooleanParam* _turn;
};

// overridden is identity
bool
ReformatPlugin::isIdentity(double time)
{
    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    clearPersistentMessage();
    
    bool flip,flop,turn;
    _flip->getValueAtTime(time, flip);
    _flop->getValueAtTime(time, flop);
    _turn->getValueAtTime(time, turn);
    if (flip || flop || turn) {
        return false;
    }
    
    int type_i;
    _type->getValue(type_i);
    ReformatTypeEnum type = (ReformatTypeEnum)type_i;
    switch (type) {
        case eReformatTypeFormat: {
            OfxRectD srcRoD = _srcClip->getRegionOfDefinition(time);
            double srcPAR = _srcClip->getPixelAspectRatio();
            int index;
            _format->getValue(index);
            double par;
            size_t w,h;
            getFormatResolution((OFX::EParamFormat)index, &w, &h, &par);
            if (srcPAR != par) {
                return false;
            }
            OfxPointD rsOne;
            rsOne.x = rsOne.y = 1.;
            OfxRectI srcRoDPixel;
            OFX::Coords::toPixelEnclosing(srcRoD, rsOne, srcPAR, &srcRoDPixel);
            if (srcRoDPixel.x1 == 0 && srcRoDPixel.y1 == 0 && srcRoDPixel.x2 == (int)w && srcRoD.y2 == (int)h) {
                return true;
            }
            break;
        }
        case eReformatTypeSize: {
            OfxRectD srcRoD = _srcClip->getRegionOfDefinition(time);
            double srcPAR = _srcClip->getPixelAspectRatio();
            OfxPointD rsOne;
            rsOne.x = rsOne.y = 1.;
            OfxRectI srcRoDPixel;
            OFX::Coords::toPixelEnclosing(srcRoD, rsOne, srcPAR, &srcRoDPixel);
            
            int w,h;
            _size->getValue(w, h);
            if (srcRoDPixel.x1 == 0 && srcRoDPixel.y1 == 0 && srcRoDPixel.x2 == w && srcRoDPixel.y2 == h) {
                return true;
            }
            break;
        }
        case eReformatTypeScale: {
            OfxPointD scaleParam = { 1., 1. };
            _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
            
            bool scaleUniform;
            _scaleUniform->getValueAtTime(time, scaleUniform);
            
            OfxPointD scale = { 1., 1. };
            ofxsTransformGetScale(scaleParam, scaleUniform, &scale);
            
            if (scale.x == 1. && scale.y == 1.) {
                return true;
            }

            break;
        }
    }
    return false;
}

bool
ReformatPlugin::getInverseTransformCanonical(double time, int /*view*/, double /*amount*/, bool invert, OFX::Matrix3x3* invtransform) const
{
    // NON-GENERIC
    OfxPointD scaleParam = { 1., 1. };

    bool scaleUniform = false;

    //compute the scale depending upon the type
    OfxRectD srcRoD = _srcClip->getRegionOfDefinition(time);

    int type_i;
    _type->getValue(type_i);
    ReformatTypeEnum type = (ReformatTypeEnum)type_i;
    switch (type) {
        case eReformatTypeFormat: {
            //specific output format
            int index;
            _format->getValue(index);
            double par;
            
            size_t w,h;
            getFormatResolution((OFX::EParamFormat)index, &w, &h, &par);
            
            
            double srcH = srcRoD.y2 - srcRoD.y1;
            double srcW = srcRoD.x2 - srcRoD.x1;
            ///Don't crash if we were provided weird RoDs
            if (srcH < 1 || srcW < 1) {
                return false;
            }
            
            OfxRectD canonicalFormat;
            OfxRectI pixelFormat;
            pixelFormat.x1 = pixelFormat.y1 = 0;
            pixelFormat.x2 = w;
            pixelFormat.y2 = h;
            OfxPointD renderScale = {1.,1.};
            OFX::Coords::toCanonical(pixelFormat, renderScale, par, &canonicalFormat);
            
            double dstH = (double)canonicalFormat.y2 - canonicalFormat.y1;
            double dstW = (double)canonicalFormat.x2 - canonicalFormat.x1;
            
            scaleParam.x = dstW / srcW;
            scaleParam.y = dstH / srcH;
          
            
        }   break;
            
        case eReformatTypeSize: {
            //size
            int w,h;
            _size->getValue(w, h);
            bool preservePar;
            _preservePAR->getValue(preservePar);
            
            double srcPar = _srcClip->getPixelAspectRatio();
            OfxPointD renderScale = {1., 1.};
            
            OfxRectI pixelSize;
            pixelSize.x1 = pixelSize.y1 = 0.;
            pixelSize.x2 = w;
            pixelSize.y2 = h;
            
            OfxRectD canonicalSize;
            OFX::Coords::toCanonical(pixelSize, renderScale, srcPar, &canonicalSize);
            
            double srcW = srcRoD.x2 - srcRoD.x1;
            double srcH = srcRoD.y2 - srcRoD.y1 ;
            double dstW = canonicalSize.x2 - canonicalSize.x1;
            double dstH = canonicalSize.y2 - canonicalSize.y1;
            
            ///Don't crash if we were provided weird RoDs
            if (srcH < 1 || srcW < 1) {
                return false;
            }
    
            
            if (preservePar) {
                
                if ((double)dstW / srcW < (double)dstH / srcH) {
                    ///Keep the given width, recompute the height
                    dstH = (int)(srcH * dstW / srcW);
                } else {
                    ///Keep the given height,recompute the width
                    dstW = (int)(srcW * dstH / srcH);
                }
                
            }
            scaleParam.x = dstH / srcW;
            scaleParam.y = dstW / srcH;
            
        }   break;
            
        case eReformatTypeScale: {
            _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
            _scaleUniform->getValueAtTime(time, scaleUniform);
        }   break;
    }

    OfxPointD scale = { 1., 1. };
    ofxsTransformGetScale(scaleParam, scaleUniform, &scale);
    
    bool flip,flop,turn;
    _flip->getValueAtTime(time, flip);
    _flop->getValueAtTime(time, flop);
    _turn->getValueAtTime(time, turn);
    
    double rot = 0;
    if (turn) {
        rot = OFX::ofxsToRadians(90);
    }
    if (flip) {
        scale.y = -scale.y;
    }
    if (flop) {
        scale.x = -scale.x;
    }
    
    bool useCenter;
    _centered->getValueAtTime(time, useCenter);
    
    
    if (useCenter) {
        OfxPointD center = {0., 0.};
        center.x = (srcRoD.x1 + srcRoD.x2) / 2.;
        center.y = (srcRoD.y1 + srcRoD.y2) / 2.;
        
        if (!invert) {
            *invtransform = ofxsMatTranslation(center.x, center.y) * ofxsMatRotation(rot) * ofxsMatScale(1. / scale.x, 1. / scale.y) * ofxsMatTranslation(-center.x, -center.y);
        } else {
            *invtransform = ofxsMatTranslation(center.x, center.y) * ofxsMatRotation(-rot) * ofxsMatScale(scale.x,scale.y) * ofxsMatTranslation(-center.x, -center.y);
            
        }
    } else {
        if (!invert) {
            *invtransform = ofxsMatRotation(rot) * ofxsMatScale(1. / scale.x, 1. / scale.y);
        } else {
            *invtransform = ofxsMatRotation(-rot) * ofxsMatScale(scale.x,scale.y);
        }
    }
    
    
    
    
    return true;
}


void
ReformatPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamType) {
        refreshVisibility();
    } else if (paramName == kParamDisableConcat) {
        refreshDynamicProps();
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
}

void
ReformatPlugin::refreshDynamicProps()
{
    if (_disableConcat) {
        bool concatDisabled;
        _disableConcat->getValue(concatDisabled);
        setCanTransform(!concatDisabled);
    }
}

void
ReformatPlugin::refreshVisibility()
{
    int type_i;
    _type->getValue(type_i);
    ReformatTypeEnum type = (ReformatTypeEnum)type_i;
    switch (type) {
        case eReformatTypeFormat:
            //specific output format
            _size->setIsSecret(true);
            _preservePAR->setIsSecret(true);
            _scale->setIsSecret(true);
            _scaleUniform->setIsSecret(true);
            _format->setIsSecret(false);
            break;
            
        case eReformatTypeSize:
            //size
            _size->setIsSecret(false);
            _preservePAR->setIsSecret(false);
            _scale->setIsSecret(true);
            _scaleUniform->setIsSecret(true);
            _format->setIsSecret(true);
            break;
            
        case eReformatTypeScale:
            //scaled
            _size->setIsSecret(true);
            _preservePAR->setIsSecret(true);
            _scale->setIsSecret(false);
            _scaleUniform->setIsSecret(false);
            _format->setIsSecret(true);
            break;
    }
}

void
ReformatPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    double par;
    int type_i;
    _type->getValue(type_i);
    ReformatTypeEnum type = (ReformatTypeEnum)type_i;
    switch (type) {
        case eReformatTypeFormat: {
            //specific output format
            int index;
            _format->getValue(index);
            size_t w,h;
            getFormatResolution((OFX::EParamFormat)index, &w, &h, &par);
            clipPreferences.setPixelAspectRatio(*_dstClip, par);
            break;
        }
        case eReformatTypeSize:
        case eReformatTypeScale:
            // don't change the pixel aspect ratio
            break;
    }
}

void
ReformatPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && args.reason == OFX::eChangeUserEdit && !_srcClipChanged->getValue()) {
        _srcClipChanged->setValue(true);
        OfxRectD srcRod = _srcClip->getRegionOfDefinition(args.time);
        double srcpar = _srcClip->getPixelAspectRatio();
        
        ///Try to find a format matching the project format in which case we switch to format mode otherwise
        ///switch to size mode and set the size accordingly
        bool foundFormat = false;
        for (int i = (int)eParamFormatPCVideo; i < (int)eParamFormatSquare2k ; ++i) {
            std::size_t w,h;
            double par;
            getFormatResolution((OFX::EParamFormat)i, &w, &h, &par);
            if (w == (srcRod.x2 - srcRod.x1) && h == (srcRod.y2 - srcRod.y1) && par == srcpar) {
                _format->setValue((OFX::EParamFormat)i);
                _type->setValue((int)eReformatTypeFormat);
                foundFormat = true;
            }
        }
        _size->setValue((int)srcRod.x2 - srcRod.x1, (int)srcRod.y2 - srcRod.y1);
        if (!foundFormat) {
            _type->setValue((int)eReformatTypeSize);
        }
        
    }
}


mDeclarePluginFactory(ReformatPluginFactory, {}, {});


void ReformatPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    desc.setSupportsMultipleClipPARs(true);
    Transform3x3Describe(desc, false);
    gHostCanTransform = false;
    
#ifdef OFX_EXTENSIONS_NUKE
    if (OFX::getImageEffectHostDescription()->canTransform) {
        gHostCanTransform = true;
    }
#endif
}

void ReformatPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

    // type
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamType);
        param->setLabel(kParamTypeLabel);
        param->setHint(kParamTypeHint);
        assert(param->getNOptions() == eReformatTypeFormat);
        param->appendOption(kParamTypeOptionFormat);
        assert(param->getNOptions() == eReformatTypeSize);
        param->appendOption(kParamTypeOptionSize);
        assert(param->getNOptions() == eReformatTypeScale);
        param->appendOption(kParamTypeOptionScale);
        param->setAnimates(false);
        param->setDefault(0);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }
    
    // format
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFormat);
        param->setLabel(kParamFormatLabel);
        param->setAnimates(false);
        assert(param->getNOptions() == eParamFormatPCVideo);
        param->appendOption(kParamFormatPCVideoLabel);
        assert(param->getNOptions() == eParamFormatNTSC);
        param->appendOption(kParamFormatNTSCLabel);
        assert(param->getNOptions() == eParamFormatPAL);
        param->appendOption(kParamFormatPALLabel);
        assert(param->getNOptions() == eParamFormatHD);
        param->appendOption(kParamFormatHDLabel);
        assert(param->getNOptions() == eParamFormatNTSC169);
        param->appendOption(kParamFormatNTSC169Label);
        assert(param->getNOptions() == eParamFormatPAL169);
        param->appendOption(kParamFormatPAL169Label);
        assert(param->getNOptions() == eParamFormat1kSuper35);
        param->appendOption(kParamFormat1kSuper35Label);
        assert(param->getNOptions() == eParamFormat1kCinemascope);
        param->appendOption(kParamFormat1kCinemascopeLabel);
        assert(param->getNOptions() == eParamFormat2kSuper35);
        param->appendOption(kParamFormat2kSuper35Label);
        assert(param->getNOptions() == eParamFormat2kCinemascope);
        param->appendOption(kParamFormat2kCinemascopeLabel);
        assert(param->getNOptions() == eParamFormat4kSuper35);
        param->appendOption(kParamFormat4kSuper35Label);
        assert(param->getNOptions() == eParamFormat4kCinemascope);
        param->appendOption(kParamFormat4kCinemascopeLabel);
        assert(param->getNOptions() == eParamFormatSquare256);
        param->appendOption(kParamFormatSquare256Label);
        assert(param->getNOptions() == eParamFormatSquare512);
        param->appendOption(kParamFormatSquare512Label);
        assert(param->getNOptions() == eParamFormatSquare1k);
        param->appendOption(kParamFormatSquare1kLabel);
        assert(param->getNOptions() == eParamFormatSquare2k);
        param->appendOption(kParamFormatSquare2kLabel);
        param->setDefault(0);
        param->setHint(kParamFormatHint);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }
    
    // size
    {
        Int2DParamDescriptor* param = desc.defineInt2DParam(kParamSize);
        param->setLabel(kParamSizeLabel);
        param->setHint(kParamSizeHint);
        param->setDefault(200, 200);
        param->setDisplayRange(0, 0, 10000, 10000);
        param->setAnimates(false);
        //param->setIsSecret(true); // done in the plugin constructor
        param->setRange(1, 1, std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    
    // preserve par
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPreservePAR);
        param->setLabel(kParamPreservePARLabel);
        param->setHint(kParamPreservePARHint);
        param->setAnimates(false);
        param->setDefault(false);
        //param->setIsSecret(true); // done in the plugin constructor
        param->setDefault(true);
        page->addChild(*param);
    }

    
    // scale
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamScale);
        param->setLabel(kParamScaleLabel);
        param->setHint(kParamScaleHint);
        param->setDoubleType(eDoubleTypeScale);
        //param->setDimensionLabels("w","h");
        param->setDefault(1,1);
        param->setRange(-10000., -10000., 10000., 10000.);
        param->setDisplayRange(0.1, 0.1, 10, 10);
        param->setIncrement(0.01);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        page->addChild(*param);
        
    }
    
    // scaleUniform
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamScaleUniform);
        param->setLabel(kParamScaleUniformLabel);
        param->setHint(kParamScaleUniformHint);
        // don't check it by default: it is easy to obtain Uniform scaling using the slider or the interact
        param->setDefault(true);
        param->setAnimates(true);
        page->addChild(*param);
    
    }
    
    // center
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamReformatCenter);
        param->setLabel(kParamReformatCenterLabel);
        param->setHint(kParamReformatCenterHint);
        param->setDefault(true);
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        page->addChild(*param);
        
    }
    
    // flip
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamFlip);
        param->setLabel(kParamFlipLabel);
        param->setHint(kParamFlipHint);
        param->setDefault(false);
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        page->addChild(*param);
        
    }
    
    // flop
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamFlop);
        param->setLabel(kParamFlopLabel);
        param->setHint(kParamFlopHint);
        param->setDefault(false);
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        page->addChild(*param);
        
    }
    
    // turn
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamTurn);
        param->setLabel(kParamTurnLabel);
        param->setHint(kParamTurnHint);
        param->setDefault(false);
        param->setAnimates(true);
        page->addChild(*param);
        
    }

    // clamp, filter, black outside
    ofxsFilterDescribeParamsInterpolate2D(desc, page, /*blackOutsideDefault*/false);
    
    // srcClipChanged
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kSrcClipChanged);
        param->setDefault(false);
        param->setIsSecret(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        page->addChild(*param);
        
    }
    
    // Disable concat
    if (gHostCanTransform) {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamDisableConcat);
        param->setLabel(kParamDisableConcatLabel);
        param->setHint(kParamDisableConcatHint);
        param->setDefault(false);
        param->setAnimates(false);
        param->setEvaluateOnChange(true);
        page->addChild(*param);

    }

}

OFX::ImageEffect* ReformatPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new ReformatPlugin(handle);
}


static ReformatPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
