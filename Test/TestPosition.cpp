/*
 OFX TestPosition plugin.
 
 Copyright (C) 2015 INRIA
 
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

#include "TestPosition.h"
#include "ofxsTransform3x3.h"
#include "ofxsTransformInteract.h"

#include <cmath>
#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#define kPluginPositionName "TestPosition"
#define kPluginPositionGrouping "Transform"
#define kPluginPositionDescription "DO NOT USE. Use the Position plugin instead. This is a plugin to test https://github.com/MrKepzie/Natron/issues/522 . A bug happens in Natron if you zoom, change the Translate parameter, and dezoom."
#define kPluginPositionIdentifier "net.sf.openfx.TestPosition"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

using namespace OFX;



#define kParamPositionTranslate kParamTransformTranslate
#define kParamPositionTranslateLabel kParamTransformTranslateLabel
#define kParamPositionTranslateHint "New position of the bottom-left pixel. Rounded to the closest pixel."

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TestPositionPlugin : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    TestPositionPlugin(OfxImageEffectHandle handle)
    : Transform3x3Plugin(handle, /*masked=*/true, false) // plugin is masked because it cannot be composed downwards
    , _translate(0)
    {
        // NON-GENERIC
        _translate = fetchDouble2DParam(kParamPositionTranslate);
        assert(_translate);
    }

private:
    virtual bool isIdentity(double time) OVERRIDE FINAL;

    virtual bool getInverseTransformCanonical(double time, double amount, bool invert, OFX::Matrix3x3* invtransform) const OVERRIDE FINAL;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;


    // NON-GENERIC
    Double2DParam* _translate;
};

// overridden is identity
bool
TestPositionPlugin::isIdentity(double time)
{
    double x, y;
    _translate->getValueAtTime(time, x, y);

    if (std::floor(x+0.5) == 0. && std::floor(y+0.5) == 0.) {
        return true;
    }

    return false;
}

bool
TestPositionPlugin::getInverseTransformCanonical(double time, double /*amount*/, bool invert, OFX::Matrix3x3* invtransform) const
{
    double x, y;
    _translate->getValueAtTime(time, x, y);

    invtransform->a = 1.;
    invtransform->b = 0.;
    invtransform->c = invert ? x : -x;
    invtransform->d = 0.;
    invtransform->e = 1.;
    invtransform->f = invert ? y : -y;
    invtransform->g = 0.;
    invtransform->h = 0.;
    invtransform->i = 1.;

    return true;
}

void
TestPositionPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamPositionTranslate) {
        changedTransform(args);
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
}






mDeclarePluginFactory(TestPositionPluginFactory, {}, {});

void TestPositionPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginPositionName);
    desc.setPluginGrouping(kPluginPositionGrouping);
    desc.setPluginDescription(kPluginPositionDescription);

    Transform3x3Describe(desc, /*masked=*/true);
}

void TestPositionPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, /*masked=*/true);

    // translate
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamPositionTranslate);
        param->setLabel(kParamPositionTranslateLabel);
        param->setHint(kParamPositionTranslateHint);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(eCoordinatesNormalised);
        param->setDefault(0., 0.);
        param->setDisplayRange(-10000, -10000, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
        if (param->getHostHasNativeOverlayHandle()) {
            param->setUseHostOverlayHandle(true);
        }

        if (page) {
            page->addChild(*param);
        }
    }

    //Transform3x3DescribeInContextEnd(desc, context, page, /*masked=*/true);
    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect*
TestPositionPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new TestPositionPlugin(handle);
}




void getTestPositionPluginID(OFX::PluginFactoryArray &ids)
{
    {
        static TestPositionPluginFactory p(kPluginPositionIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}
