/*
 OFX Transform3x3 plugin: a base plugin for 2D homographic transform,
 represented by a 3x3 matrix.

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
 */
#ifndef __Misc__Transform3x3__
#define __Misc__Transform3x3__

#include "ofxsImageEffect.h"
#include "ofxsTransform3x3Processor.h"

#define kTransform3x3InvertParamName "invert"
#define kTransform3x3InvertParamLabel "Invert"
#define kTransform3x3InvertParamHint "Invert the transform"

#define kTransform3x3MotionBlurParamName "motionBlur"
#define kTransform3x3MotionBlurParamLabel "Motion Blur"
#define kTransform3x3MotionBlurParamHint "Number of motion blur samples. 0 disables motion blur, 1 is a good value. Increasing this slows down rendering."

#define kTransform3x3ShutterParamName "shutter"
#define kTransform3x3ShutterParamLabel "Shutter"
#define kTransform3x3ShutterParamHint "Controls how long (in frames) the shutter should remain open."

#define kTransform3x3ShutterOffsetParamName "shutterOffset"
#define kTransform3x3ShutterOffsetParamLabel "Shutter Offset"
#define kTransform3x3ShutterOffsetParamHint "Controls when the shutter should be open/closed. Ignored if there is no motion blur (i.e. shutter=0 or motionBlur=0)."
#define kTransform3x3ShutterOffsetCentered 0
#define kTransform3x3ShutterOffsetCenteredLabel "centred"
#define kTransform3x3ShutterOffsetCenteredHint "centers the shutter around the frame (from t-shutter/2 to t+shutter/2)"
#define kTransform3x3ShutterOffsetStart 1
#define kTransform3x3ShutterOffsetStartLabel "start"
#define kTransform3x3ShutterOffsetStartHint "open the shutter at the frame (from t to t+shutter)"
#define kTransform3x3ShutterOffsetEnd 2
#define kTransform3x3ShutterOffsetEndLabel "end"
#define kTransform3x3ShutterOffsetEndHint "close the shutter at the frame (from t-shutter to t)"
#define kTransform3x3ShutterOffsetCustom 3
#define kTransform3x3ShutterOffsetCustomLabel "custom"
#define kTransform3x3ShutterOffsetCustomHint "open the shutter at t+shuttercustomoffset (from t+shuttercustomoffset to t+shuttercustomoffset+shutter)"

#define kTransform3x3ShutterCustomOffsetParamName "shutterCustomOffset"
#define kTransform3x3ShutterCustomOffsetParamLabel "Custom Offset"
#define kTransform3x3ShutterCustomOffsetParamHint "When custom is selected, the shutter is open at current time plus this offset (in frames). Ignored if there is no motion blur (i.e. shutter=0 or motionBlur=0)."

namespace OFX {

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class Transform3x3Plugin : public OFX::ImageEffect
{
protected:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;

public:
    /** @brief ctor */
    Transform3x3Plugin(OfxImageEffectHandle handle, bool masked);

    /** @brief destructor */
    virtual ~Transform3x3Plugin();

    // a default implementation of isIdentity is provided, which may be overridden by the derived class
    virtual bool isIdentity(double time) { return false; };

    /** @brief recover a transform matrix from an effect */
    virtual bool getInverseTransformCanonical(double time, bool invert, OFX::Matrix3x3* invtransform) const = 0;



    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) /*OVERRIDE FINAL*/;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) /*OVERRIDE FINAL*/;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) /*OVERRIDE FINAL*/;

    // override isIdentity
    virtual bool isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &identityTime) /*OVERRIDE FINAL*/;

#ifdef OFX_EXTENSIONS_NUKE
    /** @brief recover a transform matrix from an effect */
    virtual bool getTransform(const OFX::TransformArguments &args, OFX::Clip * &transformClip, double transformMatrix[9]);
#endif

    // override changedParam. note that the derived class MUST explicitely call this method after handling its own parameter changes
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);

    // override purgeCaches.
    virtual void purgeCaches();

    // this method must be called by the derived class when the transform was changed
    void changedTransform(const OFX::InstanceChangedArgs &args);

private:
    /* internal render function */
    template <class PIX, int nComponents, int maxValue, bool masked>
    void renderInternalForBitDepth(const OFX::RenderArguments &args);

    template <int nComponents, bool masked>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(Transform3x3ProcessorBase &, const OFX::RenderArguments &args);

    bool isIdentity(double time, OFX::Clip * &identityClip, double &identityTime);

    size_t getInverseTransforms(double time,
                                OfxPointD renderscale,
                                bool fielded,
                                double pixelaspectratio,
                                bool invert,
                                double shutter,
                                int shutteroffset,
                                double shuttercustomoffset,
                                OFX::Matrix3x3* invtransform,
                                size_t invtransformsizealloc) const;

    void transformRegion(const OfxRectD &rectFrom, double time, bool invert, double motionblur, double shutter, int shutteroffset_i, double shuttercustomoffset, OfxRectD *rectTo);
    
private:
    class CacheID;

    template<class ID>
    class Cache;

    Cache<CacheID>* _cache;

    // Transform3x3-GENERIC
    OFX::BooleanParam* _invert;
    // GENERIC
    OFX::ChoiceParam* _filter;
    OFX::BooleanParam* _clamp;
    OFX::BooleanParam* _blackOutside;
    OFX::DoubleParam* _motionblur;
    OFX::DoubleParam* _shutter;
    OFX::ChoiceParam* _shutteroffset;
    OFX::DoubleParam* _shuttercustomoffset;

    bool _masked;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};

void Transform3x3Describe(OFX::ImageEffectDescriptor &desc, bool masked);
OFX::PageParamDescriptor * Transform3x3DescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, bool masked);
void Transform3x3DescribeInContextEnd(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor* page, bool masked);

}
#endif /* defined(__Misc__Transform3x3__) */
