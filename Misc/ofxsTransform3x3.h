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

#define kTransform3x3InvertParamName "Invert"

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

    // a default implementation of isIdentity is provided, which may be overridden by the derived class
    virtual bool isIdentity(double time) { return false; };

    /** @brief recover a transform matrix from an effect */
    virtual bool getInverseTransformCanonical(double time, bool invert, OFX::Matrix3x3* invtransform) = 0;



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

private:
    /* internal render function */
    template <class PIX, int nComponents, int maxValue, bool masked>
    void renderInternalForBitDepth(const OFX::RenderArguments &args);

    template <int nComponents, bool masked>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(Transform3x3ProcessorBase &, const OFX::RenderArguments &args);

    bool isIdentity_internal(double time, OFX::Clip * &identityClip, double &identityTime);
    
private:
    // Transform3x3-GENERIC
    OFX::BooleanParam* _invert;
    // GENERIC
    OFX::ChoiceParam* _filter;
    OFX::BooleanParam* _clamp;
    OFX::BooleanParam* _blackOutside;
    bool _masked;
    OFX::DoubleParam* _mix;
};

void Transform3x3Describe(OFX::ImageEffectDescriptor &desc, bool masked);
OFX::PageParamDescriptor * Transform3x3DescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, bool masked);
void Transform3x3DescribeInContextEnd(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor* page, bool masked);

}
#endif /* defined(__Misc__Transform3x3__) */
