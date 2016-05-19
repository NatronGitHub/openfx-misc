/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2016 INRIA
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
 * OFX TestPosition plugin.
 */

#include "ofxsTransform3x3.h"
#include "ofxsTransformInteract.h"

#include <cmath>
#include <cfloat>
#include <iostream>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#define kPluginPositionName "TestPosition"
#define kPluginPositionGrouping "Other/Test"
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
class TestPositionPlugin
    : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    TestPositionPlugin(OfxImageEffectHandle handle)
        : Transform3x3Plugin(handle, /*masked=*/ true, eTransform3x3ParamsTypeMotionBlur) // plugin is masked because it cannot be composed downwards
        , _translate(0)
    {
        // NON-GENERIC
        _translate = fetchDouble2DParam(kParamPositionTranslate);
        assert(_translate);
    }

private:
    virtual bool isIdentity(double time) OVERRIDE FINAL;
    virtual bool getInverseTransformCanonical(double time, int view, double amount, bool invert, OFX::Matrix3x3* invtransform) const OVERRIDE FINAL;
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

    if ( (std::floor(x + 0.5) == 0.) && (std::floor(y + 0.5) == 0.) ) {
        return true;
    }

    return false;
}

bool
TestPositionPlugin::getInverseTransformCanonical(double time,
                                                 int /*view*/,
                                                 double /*amount*/,
                                                 bool invert,
                                                 OFX::Matrix3x3* invtransform) const
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
TestPositionPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                                 const std::string &paramName)
{
    if (paramName == kParamPositionTranslate) {
        changedTransform(args);
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
}

mDeclarePluginFactory(TestPositionPluginFactory, {}, {});
void
TestPositionPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginPositionName);
    desc.setPluginGrouping(kPluginPositionGrouping);
    desc.setPluginDescription(kPluginPositionDescription);

    Transform3x3Describe(desc, /*masked=*/ true);
}

void
TestPositionPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                             OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, /*masked=*/ true);

    // translate
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamPositionTranslate);
        param->setLabel(kParamPositionTranslateLabel);
        param->setHint(kParamPositionTranslateHint);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(eCoordinatesNormalised);
        param->setDefault(0., 0.);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(-10000, -10000, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
        if ( param->getHostHasNativeOverlayHandle() ) {
            param->setUseHostNativeOverlayHandle(true);
        }

        if (page) {
            page->addChild(*param);
        }
    }

    //Transform3x3DescribeInContextEnd(desc, context, page, /*masked=*/true);
    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect*
TestPositionPluginFactory::createInstance(OfxImageEffectHandle handle,
                                          OFX::ContextEnum /*context*/)
{
    return new TestPositionPlugin(handle);
}

static TestPositionPluginFactory p(kPluginPositionIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
