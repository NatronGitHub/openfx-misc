/*
 OFX utilities for tracking.
 
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

#include "ofxsTracking.h"

#include "ofxsOGLTextRenderer.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

using namespace OFX;

GenericTrackerPlugin::GenericTrackerPlugin(OfxImageEffectHandle handle)
: ImageEffect(handle)
, dstClip_(0)
, srcClip_(0)
, _center(0)
, _innerBtmLeft(0)
, _innerTopRight(0)
, _outerBtmLeft(0)
, _outerTopRight(0)
, _backwardButton(0)
, _prevButton(0)
, _nextButton(0)
, _forwardButton(0)
, _instanceName(0)
{
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA);

    
    _center = fetchDouble2DParam(kTrackCenterPointParamName);
    _innerBtmLeft = fetchDouble2DParam(kTrackPatternBoxBottomLeftParamName);
    _innerTopRight = fetchDouble2DParam(kTrackPatternBoxTopRightParamName);
    _outerBtmLeft = fetchDouble2DParam(kTrackSearchBoxBottomLeftParamName);
    _outerTopRight = fetchDouble2DParam(kTrackSearchBoxTopRightParamName);
    _backwardButton = fetchPushButtonParam(kTrackBackwardParamName);
    _prevButton = fetchPushButtonParam(kTrackPreviousParamName);
    _nextButton = fetchPushButtonParam(kTrackNextParamName);
    _forwardButton = fetchPushButtonParam(kTrackForwardParamName);
    _instanceName = fetchStringParam(kOfxParamStringSublabelName);
    assert(_center && _innerTopRight && _innerBtmLeft && _outerTopRight && _outerBtmLeft && _backwardButton && _prevButton && _nextButton && _forwardButton && _instanceName);
}

bool GenericTrackerPlugin::isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime)
{
    identityClip = srcClip_;
    identityTime = args.time;
    return true;
}


void GenericTrackerPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    
    if (paramName == kTrackBackwardParamName) {
        OFX::TrackArguments trackArgs;
        trackArgs.first = args.time;
        double first,last;
        timeLineGetBounds(first, last);
        trackArgs.last = first + 1;
        if (trackArgs.last <= trackArgs.first) {
            trackArgs.forward = false;
            trackArgs.reason = args.reason;
            trackRange(trackArgs);
        }
    } else if (paramName == kTrackPreviousParamName) {
        OFX::TrackArguments trackArgs;
        trackArgs.first = args.time;
        trackArgs.last = trackArgs.first;
        trackArgs.forward = false;
        trackArgs.reason = args.reason;
        trackRange(trackArgs);
    } else if (paramName == kTrackNextParamName) {
        OFX::TrackArguments trackArgs;
        trackArgs.first = args.time;
        trackArgs.last = trackArgs.first;
        trackArgs.forward = true;
        trackArgs.reason = args.reason;
        trackRange(trackArgs);
    } else if (paramName == kTrackForwardParamName) {
        OFX::TrackArguments trackArgs;
        trackArgs.first = args.time;
        double first,last;
        timeLineGetBounds(first, last);
        trackArgs.last = last - 1;
        if (trackArgs.last >= trackArgs.first) {
            trackArgs.forward = true;
            trackArgs.reason = args.reason;
            trackRange(trackArgs);
        }
    }
    
}

void genericTrackerDescribe(OFX::ImageEffectDescriptor &desc)
{
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);
    
    // supported bit depths depend on the tracking algorithm.
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
    //desc.addSupportedBitDepth(eBitDepthFloat);
    
    // single instance depends on the algorithm
    //desc.setSingleInstance(false);

    // no host frame threading (anyway, the tracker always returns identity)
    desc.setHostFrameThreading(false);
    
    ///We do temporal clip access
    desc.setTemporalClipAccess(true);

    // rendertwicealways must be set to true if the tracker cannot handle interlaced content (most don't)
    //desc.setRenderTwiceAlways(true);
    
    desc.setSupportsMultipleClipPARs(false);
    
    // support multithread (anyway, the tracker always returns identity)
    desc.setRenderThreadSafety(eRenderFullySafe);
    
    // support tiles (anyway, the tracker always returns identity)
    desc.setSupportsTiles(true);
    
    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    
    ///We don't support multi-resolution
    desc.setSupportsMultiResolution(false);
    

}

OFX::PageParamDescriptor* genericTrackerDescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    
    ///we do temporal clip access
    srcClip->setTemporalClipAccess(true);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);
    srcClip->setOptional(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(true);
    
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    return page;
}


void genericTrackerDescribePointParameters(OFX::ImageEffectDescriptor &desc,OFX::PageParamDescriptor* page)
{
    OFX::Double2DParamDescriptor* center = desc.defineDouble2DParam(kTrackCenterPointParamName);
    center->setLabels(kTrackCenterPointParamLabel, kTrackCenterPointParamLabel, kTrackCenterPointParamLabel);
    center->setHint(kTrackCenterPointParamHint);
    center->setDoubleType(eDoubleTypeXYAbsolute);
    center->setDefaultCoordinateSystem(eCoordinatesNormalised);
    center->setDefault(0.5, 0.5);
    center->getPropertySet().propSetInt(kOfxParamPropPluginMayWrite, 1);
    page->addChild(*center);
    
    
    OFX::Double2DParamDescriptor* innerBtmLeft = desc.defineDouble2DParam(kTrackPatternBoxBottomLeftParamName);
    innerBtmLeft->setLabels(kTrackPatternBoxBottomLeftParamLabel, kTrackPatternBoxBottomLeftParamLabel, kTrackPatternBoxBottomLeftParamLabel);
    innerBtmLeft->setHint(kTrackPatternBoxBottomLeftParamHint);
    innerBtmLeft->setDoubleType(eDoubleTypeXY);
    innerBtmLeft->setDefaultCoordinateSystem(eCoordinatesCanonical);
    innerBtmLeft->setDefault(-15,-15);
    //innerBtmLeft->setIsSecret(true);
    innerBtmLeft->getPropertySet().propSetInt(kOfxParamPropPluginMayWrite, 1);
    page->addChild(*innerBtmLeft);
    
    OFX::Double2DParamDescriptor* innerTopRight = desc.defineDouble2DParam(kTrackPatternBoxTopRightParamName);
    innerTopRight->setLabels(kTrackPatternBoxTopRightParamLabel, kTrackPatternBoxTopRightParamLabel, kTrackPatternBoxTopRightParamLabel);
    innerTopRight->setHint(kTrackPatternBoxTopRightParamHint);
    innerTopRight->setDoubleType(eDoubleTypeXY);
    innerTopRight->setDefaultCoordinateSystem(eCoordinatesCanonical);
    innerTopRight->setDefault(15, 15);
    //innerTopRight->setIsSecret(true);
    innerTopRight->getPropertySet().propSetInt(kOfxParamPropPluginMayWrite, 1);
    page->addChild(*innerTopRight);
    
    OFX::Double2DParamDescriptor* outerBtmLeft = desc.defineDouble2DParam(kTrackSearchBoxBottomLeftParamName);
    outerBtmLeft->setLabels(kTrackSearchBoxBottomLeftParamLabel, kTrackSearchBoxBottomLeftParamLabel, kTrackSearchBoxBottomLeftParamLabel);
    outerBtmLeft->setHint(kTrackSearchBoxBottomLeftParamHint);
    outerBtmLeft->setDoubleType(eDoubleTypeXY);
    outerBtmLeft->setDefaultCoordinateSystem(eCoordinatesCanonical);
    outerBtmLeft->setDefault(-25,-25);
    //outerBtmLeft->setIsSecret(true);
    outerBtmLeft->getPropertySet().propSetInt(kOfxParamPropPluginMayWrite, 1);
    page->addChild(*outerBtmLeft);
    
    OFX::Double2DParamDescriptor* outerTopRight = desc.defineDouble2DParam(kTrackSearchBoxTopRightParamName);
    outerTopRight->setLabels(kTrackSearchBoxTopRightParamLabel, kTrackSearchBoxTopRightParamLabel, kTrackSearchBoxTopRightParamLabel);
    outerTopRight->setHint(kTrackSearchBoxBottomLeftParamHint);
    outerTopRight->setDoubleType(eDoubleTypeXY);
    outerTopRight->setDefaultCoordinateSystem(eCoordinatesCanonical);
    outerTopRight->setDefault(25, 25);
    //outerTopRight->setIsSecret(true);
    outerTopRight->getPropertySet().propSetInt(kOfxParamPropPluginMayWrite, 1);
    page->addChild(*outerTopRight);
    
    OFX::PushButtonParamDescriptor* backward = desc.definePushButtonParam(kTrackBackwardParamName);
    backward->setLabels(kTrackBackwardParamLabel, kTrackBackwardParamLabel,kTrackBackwardParamLabel);
    backward->setHint(kTrackBackwardParamHint);
    backward->setLayoutHint(eLayoutHintNoNewLine);
    page->addChild(*backward);

    
    OFX::PushButtonParamDescriptor* prev = desc.definePushButtonParam(kTrackPreviousParamName);
    prev->setLabels(kTrackPreviousParamLabel, kTrackPreviousParamLabel, kTrackPreviousParamLabel);
    prev->setHint(kTrackPreviousParamHint);
    prev->setLayoutHint(eLayoutHintNoNewLine);
    page->addChild(*prev);
    
    OFX::PushButtonParamDescriptor* next = desc.definePushButtonParam(kTrackNextParamName);
    next->setLabels(kTrackNextParamLabel, kTrackNextParamLabel, kTrackNextParamLabel);
    next->setHint(kTrackNextParamHint);
    next->setLayoutHint(eLayoutHintNoNewLine);
    page->addChild(*next);

    OFX::PushButtonParamDescriptor* forward = desc.definePushButtonParam(kTrackForwardParamName);
    forward->setLabels(kTrackForwardParamLabel, kTrackForwardParamLabel, kTrackForwardParamLabel);
    forward->setHint(kTrackForwardParamHint);
    page->addChild(*forward);


    OFX::StringParamDescriptor* name = desc.defineStringParam(kTrackLabelParamName);
    name->setLabels(kTrackLabelParamLabel, kTrackLabelParamLabel, kTrackLabelParamLabel);
    name->setHint(kTrackLabelParamHint);
    name->setDefault(kTrackLabelParamDefault);
    name->setIsSecret(false); // it has to be user-editable
    name->setEnabled(true); // it has to be user-editable
    name->setIsPersistant(true); // it has to be saved with the instance parameters
    name->setEvaluateOnChange(false);
    page->addChild(*name);
    
}

//////////////////// INTERACT ////////////////////

bool TrackerRegionInteract::isNearbyTopLeft(const OfxPointD& pos, double tolerance, const OfxPointD& btmLeft, const OfxPointD& topRight) const
{
    OfxPointD topLeft;
    topLeft.x = btmLeft.x;
    topLeft.y = topRight.y;
    if (pos.x >= (topLeft.x - tolerance) && pos.x <= (topLeft.x + tolerance) &&
        pos.y >= (topLeft.y - tolerance) && pos.y <= (topLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool TrackerRegionInteract::isNearbyTopRight(const OfxPointD& pos, double tolerance, const OfxPointD& btmLeft, const OfxPointD& topRight) const
{
    if (pos.x >= (topRight.x - tolerance) && pos.x <= (topRight.x + tolerance) &&
        pos.y >= (topRight.y - tolerance) && pos.y <= (topRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool TrackerRegionInteract::isNearbyBtmLeft(const OfxPointD& pos, double tolerance, const OfxPointD& btmLeft, const OfxPointD& topRight) const
{
    if (pos.x >= (btmLeft.x - tolerance) && pos.x <= (btmLeft.x + tolerance) &&
        pos.y >= (btmLeft.y - tolerance) && pos.y <= (btmLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool TrackerRegionInteract::isNearbyBtmRight(const OfxPointD& pos, double tolerance, const OfxPointD& btmLeft, const OfxPointD& topRight) const
{
    OfxPointD btmRight;
    btmRight.x = topRight.x;
    btmRight.y = btmLeft.y ;
    if (pos.x >= (btmRight.x - tolerance) && pos.x <= (btmRight.x + tolerance) &&
        pos.y >= (btmRight.y - tolerance) && pos.y <= (btmRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool TrackerRegionInteract::isNearbyMidTop(const OfxPointD& pos, double tolerance, const OfxPointD& btmLeft, const OfxPointD& topRight) const
{
    OfxPointD midTop;
    midTop.x = (btmLeft.x + topRight.x) / 2.;
    midTop.y = topRight.y;
    if (pos.x >= (midTop.x - tolerance) && pos.x <= (midTop.x + tolerance) &&
        pos.y >= (midTop.y - tolerance) && pos.y <= (midTop.y + tolerance)) {
        return true;
    } else {
        return false;
    }
    
}

bool TrackerRegionInteract::isNearbyMidRight(const OfxPointD& pos, double tolerance, const OfxPointD& btmLeft, const OfxPointD& topRight) const
{
    OfxPointD midRight;
    midRight.x = topRight.x;
    midRight.y = (btmLeft.y + topRight.y) / 2.;
    if (pos.x >= (midRight.x - tolerance) && pos.x <= (midRight.x + tolerance) &&
        pos.y >= (midRight.y - tolerance) && pos.y <= (midRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool TrackerRegionInteract::isNearbyMidLeft(const OfxPointD& pos,double tolerance, const OfxPointD& btmLeft, const OfxPointD& topRight) const
{
    OfxPointD midLeft;
    midLeft.x = btmLeft.x;
    midLeft.y = (btmLeft.y + topRight.y) /2.;
    if (pos.x >= (midLeft.x - tolerance) && pos.x <= (midLeft.x + tolerance) &&
        pos.y >= (midLeft.y - tolerance) && pos.y <= (midLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }
    
}

bool TrackerRegionInteract::isNearbyMidBtm(const OfxPointD& pos,double tolerance, const OfxPointD& btmLeft, const OfxPointD& topRight) const
{
    OfxPointD midBtm;
    midBtm.x = (btmLeft.x + topRight.x) / 2.;
    midBtm.y = btmLeft.y ;
    if (pos.x >= (midBtm.x - tolerance) && pos.x <= (midBtm.x + tolerance) &&
        pos.y >= (midBtm.y - tolerance) && pos.y <= (midBtm.y + tolerance)) {
        return true;
    } else {
        return false;
    }
    
}

bool TrackerRegionInteract::isNearbyCenter(const OfxPointD& pos,double tolerance,const OfxPointD& center) const
{
    return (pos.x >= (center.x - tolerance) && pos.x <= (center.x + tolerance) &&
            pos.y >= (center.y - tolerance) && pos.y <= (center.y + tolerance));
}



bool TrackerRegionInteract::draw(const OFX::DrawArgs &args)
{
    OfxPointD innerBtmLeft,innerTopRight,outerBtmLeft,outerTopRight,center;
    
    if (_ms != eIdle) {
        innerBtmLeft = _innerBtmLeftDragPos;
        innerTopRight = _innerTopRightDragPos;
        outerBtmLeft = _outerBtmLeftDragPos;
        outerTopRight = _outerTopRightDragPos;
        center = _centerDragPos;

    } else {
        _center->getValueAtTime(args.time, center.x, center.y);

        _innerBtmLeft->getValueAtTime(args.time, innerBtmLeft.x, innerBtmLeft.y);
        _innerTopRight->getValueAtTime(args.time, innerTopRight.x, innerTopRight.y);
        
        ///innerBtmLeft is relative to the center, make it absolute
        innerBtmLeft.x += center.x;
        innerBtmLeft.y += center.y;
        innerTopRight.x += center.x;
        innerTopRight.y += center.y;

        _outerBtmLeft->getValueAtTime(args.time, outerBtmLeft.x, outerBtmLeft.y);
        _outerTopRight->getValueAtTime(args.time, outerTopRight.x, outerTopRight.y);

        
        ///outerBtmLeft is relative to the center, make it absolute
        outerBtmLeft.x += center.x;
        outerBtmLeft.y += center.y;
        outerTopRight.x += center.x;
        outerTopRight.y += center.y;
    }
    
    
    ///Compute all other points positions given the 5 parameters retrieved above
    OfxPointD innerTopLeft, innerMidTop, innerMidRight, innerBtmRight, innerMidBtm, innerMidLeft;
    OfxPointD outerTopLeft, outerMidTop, outerMidRight, outerBtmRight, outerMidBtm, outerMidLeft;
    innerTopLeft.x = innerBtmLeft.x;
    innerTopLeft.y = innerTopRight.y;
    innerMidTop.x = (innerBtmLeft.x + innerTopRight.x) / 2.;
    innerMidTop.y = innerTopLeft.y;
    innerMidRight.x = innerTopRight.x;
    innerMidRight.y = (innerBtmLeft.y + innerTopRight.y) / 2.;
    innerBtmRight.x = innerTopRight.x;
    innerBtmRight.y = innerBtmLeft.y;
    innerMidBtm.x = innerMidTop.x;
    innerMidBtm.y = innerBtmLeft.y;
    innerMidLeft.x = innerBtmLeft.x;
    innerMidLeft.y = innerMidRight.y;
    
    outerTopLeft.x = outerBtmLeft.x;
    outerTopLeft.y = outerTopRight.y;
    outerMidTop.x = (outerBtmLeft.x + outerTopRight.x) / 2.;
    outerMidTop.y = outerTopLeft.y;
    outerMidRight.x = outerTopRight.x;
    outerMidRight.y = (outerBtmLeft.y + outerTopRight.y) / 2.;
    outerBtmRight.x = outerTopRight.x;
    outerBtmRight.y = outerBtmLeft.y;
    outerMidBtm.x = outerMidTop.x;
    outerMidBtm.y = outerBtmLeft.y;
    outerMidLeft.x = outerBtmLeft.x;
    outerMidLeft.y = outerMidRight.y;


    
    glColor4f(0.9, 0.9, 0.9, 1.);
    glBegin(GL_LINE_STRIP);
    glVertex2d(innerBtmLeft.x, innerBtmLeft.y);
    glVertex2d(innerTopLeft.x, innerTopLeft.y);
    glVertex2d(innerTopRight.x, innerTopRight.y);
    glVertex2d(innerBtmRight.x, innerBtmRight.y);
    glVertex2d(innerBtmLeft.x, innerBtmLeft.y);
    glEnd();
    
    glBegin(GL_LINE_STRIP);
    glVertex2d(outerBtmLeft.x, outerBtmLeft.y);
    glVertex2d(outerTopLeft.x, outerTopLeft.y);
    glVertex2d(outerTopRight.x, outerTopRight.y);
    glVertex2d(outerBtmRight.x, outerBtmRight.y);
    glVertex2d(outerBtmLeft.x, outerBtmLeft.y);
    glEnd();
    
    glPointSize(6);
    glBegin(GL_POINTS);
    
    //////DRAWING INNER POINTS
    if (_ds == eHoveringInnerBottomLeft || _ms == eDraggingInnerBottomLeft) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(innerBtmLeft.x, innerBtmLeft.y);
    }
    if (_ds == eHoveringInnerMidLeft || _ms == eDraggingInnerMidLeft) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(innerMidLeft.x, innerMidLeft.y);
    }
    if (_ds == eHoveringInnerTopLeft || _ms == eDraggingInnerTopLeft) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(innerTopLeft.x, innerTopLeft.y);
    }
    
    if (_ds == eHoveringInnerMidTop || _ms == eDraggingInnerMidTop) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(innerMidTop.x, innerMidTop.y);
    }
    
    if (_ds == eHoveringInnerTopRight || _ms == eDraggingInnerTopRight) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(innerTopRight.x, innerTopRight.y);
    }
    
    if (_ds == eHoveringInnerMidRight || _ms == eDraggingInnerMidRight) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(innerMidRight.x, innerMidRight.y);
    }
    
    if (_ds == eHoveringInnerBottomRight || _ms == eDraggingInnerBottomRight) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(innerBtmRight.x, innerBtmRight.y);
    }
    
    if (_ds == eHoveringInnerMidBtm || _ms == eDraggingInnerMidBtm) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(innerMidBtm.x, innerMidBtm.y);
    }
    
    //////DRAWING OUTTER POINTS
    
    if (_ds == eHoveringOuterBottomLeft || _ms == eDraggingOuterBottomLeft) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(outerBtmLeft.x, outerBtmLeft.y);
    }
    
    if (_ds == eHoveringOuterMidLeft || _ms == eDraggingOuterMidLeft) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(outerMidLeft.x, outerMidLeft.y);
    }
    
    if (_ds == eHoveringOuterTopLeft || _ms == eDraggingOuterTopLeft) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(outerTopLeft.x, outerTopLeft.y);
    }
    if (_ds == eHoveringOuterMidTop || _ms == eDraggingOuterMidTop) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(outerMidTop.x, outerMidTop.y);
    }
    
    if (_ds == eHoveringOuterTopRight || _ms == eDraggingOuterTopRight) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(outerTopRight.x, outerTopRight.y);
    }
    
    if (_ds == eHoveringOuterMidRight || _ms == eDraggingOuterMidRight) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(outerMidRight.x, outerMidRight.y);
    }
    
    if (_ds == eHoveringOuterBottomRight || _ms == eDraggingOuterBottomRight) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(outerBtmRight.x, outerBtmRight.y);
    }
    
    if (_ds == eHoveringOuterMidBtm || _ms == eDraggingOuterMidBtm) {
        glColor4f(0., 1., 0., 1.);
        glVertex2d(outerMidBtm.x, outerMidBtm.y);
    }
    ///draw center
    if (_ds == eHoveringCenter || _ms == eDraggingCenter) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(center.x,center.y);
    glEnd();
    glPointSize(1);

    double handleSizeX = 10. * args.pixelScale.x;
    double handleSizeY = 10. * args.pixelScale.y;
    
    ///now show small lines at handle positions
    glBegin(GL_LINES);

    if (_ds == eHoveringInnerMidLeft || _ms == eDraggingInnerMidLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(0.8, 0.8, 0.8, 0.8);
    }
    glVertex2d(innerMidLeft.x, innerMidLeft.y);
    glVertex2d(innerMidLeft.x - handleSizeX, innerMidLeft.y);
    
    if (_ds == eHoveringInnerMidTop || _ms == eDraggingInnerMidTop) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(0.8, 0.8, 0.8, 0.8);
    }
    glVertex2d(innerMidTop.x, innerMidTop.y);
    glVertex2d(innerMidTop.x, innerMidTop.y + handleSizeY);
    

    if (_ds == eHoveringInnerMidRight || _ms == eDraggingInnerMidRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(0.8, 0.8, 0.8, 0.8);
    }
    glVertex2d(innerMidRight.x, innerMidRight.y);
    glVertex2d(innerMidRight.x + handleSizeX, innerMidRight.y);

    if (_ds == eHoveringInnerMidBtm || _ms == eDraggingInnerMidBtm) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(0.8, 0.8, 0.8, 0.8);
    }
    glVertex2d(innerMidBtm.x, innerMidBtm.y);
    glVertex2d(innerMidBtm.x, innerMidBtm.y - handleSizeY);
    
    //////DRAWING OUTTER HANDLES
    
    if (_ds == eHoveringOuterMidLeft || _ms == eDraggingOuterMidLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(0.8, 0.8, 0.8, 0.8);
    }
    glVertex2d(outerMidLeft.x, outerMidLeft.y);
    glVertex2d(outerMidLeft.x - handleSizeX, outerMidLeft.y);

    if (_ds == eHoveringOuterMidTop || _ms == eDraggingOuterMidTop) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(0.8, 0.8, 0.8, 0.8);
    }
    glVertex2d(outerMidTop.x, outerMidTop.y);
    glVertex2d(outerMidTop.x, outerMidTop.y + handleSizeY);
    
    if (_ds == eHoveringOuterMidRight || _ms == eDraggingOuterMidRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(0.8, 0.8, 0.8, 0.8);
    }
    glVertex2d(outerMidRight.x + handleSizeX, outerMidRight.y);
    glVertex2d(outerMidRight.x, outerMidRight.y);


    
    if (_ds == eHoveringOuterMidBtm || _ms == eDraggingOuterMidBtm) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(0.8, 0.8, 0.8, 0.8);
    }
    glVertex2d(outerMidBtm.x, outerMidBtm.y);
    glVertex2d(outerMidBtm.x, outerMidBtm.y - handleSizeY);


    glEnd();
    
    
    std::string name;
    _name->getValue(name);
    TextRenderer::bitmapString(center.x,center.y + 10,name.c_str());
    
    return true;
}

bool TrackerRegionInteract::penMotion(const OFX::PenArgs &args)
{
    bool didSomething = false;
    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;
    
    double selectionTol = 8. * args.pixelScale.x;
    
    OfxPointD innerTopRight,outerTopRight,innerBtmLeft,outerBtmLeft,center;
    _innerTopRight->getValueAtTime(args.time, innerTopRight.x, innerTopRight.y);
    _outerTopRight->getValueAtTime(args.time, outerTopRight.x, outerTopRight.y);
    _innerBtmLeft->getValueAtTime(args.time, innerBtmLeft.x, innerBtmLeft.y);
    _outerBtmLeft->getValueAtTime(args.time, outerBtmLeft.x, outerBtmLeft.y);
    _center->getValueAtTime(args.time, center.x, center.y);
    ///innerBtmLeft and outerBtmLeft are relative to the center, make them absolute
    innerBtmLeft.x += center.x;
    innerBtmLeft.y += center.y;
    innerTopRight.x += center.x;
    innerTopRight.y += center.y;

    outerBtmLeft.x += center.x;
    outerBtmLeft.y += center.y;
    outerTopRight.x += center.x;
    outerTopRight.y += center.y;

    bool lastStateWasHovered = _ds != eInactive;

    if (_ms == eIdle) {
        if (isNearbyCenter(args.penPosition, selectionTol, center)) {
            _ds = eHoveringCenter;
            didSomething = true;
        } else if (isNearbyBtmLeft(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
            _ds = eHoveringInnerBottomLeft;
            didSomething = true;
        } else if (isNearbyBtmRight(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
            _ds = eHoveringInnerBottomRight;
            didSomething = true;
        } else if (isNearbyTopRight(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
            _ds = eHoveringInnerTopRight;
            didSomething = true;
        } else if (isNearbyTopLeft(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
            _ds = eHoveringInnerTopLeft;
            didSomething = true;
        } else if (isNearbyMidTop(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
            _ds = eHoveringInnerMidTop;
            didSomething = true;
        } else if (isNearbyMidRight(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
            _ds = eHoveringInnerMidRight;
            didSomething = true;
        } else if (isNearbyMidBtm(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
            _ds = eHoveringInnerMidBtm;
            didSomething = true;
        } else if (isNearbyMidLeft(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
            _ds = eHoveringInnerMidLeft;
            didSomething = true;
        } else if (isNearbyBtmLeft(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
            _ds = eHoveringOuterBottomLeft;
            didSomething = true;
        } else if (isNearbyBtmRight(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
            _ds = eHoveringOuterBottomRight;
            didSomething = true;
        } else if (isNearbyTopRight(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
            _ds = eHoveringOuterTopRight;
            didSomething = true;
        } else if (isNearbyTopLeft(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
            _ds = eHoveringOuterTopLeft;
            didSomething = true;
        } else if (isNearbyMidTop(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
            _ds = eHoveringOuterMidTop;
            didSomething = true;
        } else if (isNearbyMidRight(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
            _ds = eHoveringOuterMidRight;
            didSomething = true;
        } else if (isNearbyMidBtm(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
            _ds = eHoveringOuterMidBtm;
            didSomething = true;
        } else if (isNearbyMidLeft(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
            _ds = eHoveringOuterMidLeft;
            didSomething = true;
        } else {
            _ds = eInactive;
        }
    }
    
    double multiplier = _controlDown ? 0 : 1;
    if (_ms == eDraggingInnerBottomLeft) {
        _innerBtmLeftDragPos.x += delta.x;
        _innerBtmLeftDragPos.y += delta.y;
        _innerTopRightDragPos.x -= delta.x;
        _innerTopRightDragPos.y -= delta.y;
        ///also move the outer rect
        _outerBtmLeftDragPos.x += delta.x;
        _outerBtmLeftDragPos.y += delta.y;
        _outerTopRightDragPos.x -= delta.x;
        _outerTopRightDragPos.y -= delta.y;
        didSomething = true;
    } else if (_ms == eDraggingInnerTopLeft) {
        _innerBtmLeftDragPos.x += delta.x;
        _innerBtmLeftDragPos.y -= delta.y;
    
        _innerTopRightDragPos.y += delta.y;
        _innerTopRightDragPos.x -= delta.x;
        
        _outerBtmLeftDragPos.x += delta.x;
        _outerBtmLeftDragPos.y -= delta.y;
        
        _outerTopRightDragPos.y += delta.y;
        _outerTopRightDragPos.x -= delta.x;
        didSomething = true;
    } else if (_ms == eDraggingInnerTopRight) {
        _innerBtmLeftDragPos.x -= delta.x;
        _innerBtmLeftDragPos.y -= delta.y;
        
        _innerTopRightDragPos.y += delta.y;
        _innerTopRightDragPos.x += delta.x;
        
        _outerBtmLeftDragPos.x -= delta.x;
        _outerBtmLeftDragPos.y -= delta.y;
        
        _outerTopRightDragPos.y += delta.y;
        _outerTopRightDragPos.x += delta.x;
        didSomething = true;
    } else if (_ms == eDraggingInnerBottomRight) {
        _innerTopRightDragPos.y -= delta.y;
        _innerTopRightDragPos.x += delta.x;
        _innerBtmLeftDragPos.y += delta.y;
        _innerBtmLeftDragPos.x -= delta.x;
        
        
        _outerTopRightDragPos.y -= delta.y;
        _outerTopRightDragPos.x += delta.x;
        _outerBtmLeftDragPos.y += delta.y;
        _outerBtmLeftDragPos.x -= delta.x;
        

        didSomething = true;
    } else if (_ms == eDraggingInnerMidTop) {
        _innerBtmLeftDragPos.y -= delta.y;
        _outerBtmLeftDragPos.y -= delta.y;
        
        _innerTopRightDragPos.y += delta.y;
        _outerTopRightDragPos.y += delta.y;

        didSomething = true;
    } else if (_ms == eDraggingInnerMidRight) {
        _innerTopRightDragPos.x += delta.x;
        _outerTopRightDragPos.x += delta.x;
        _innerBtmLeftDragPos.x -= delta.x;
        _outerBtmLeftDragPos.x -= delta.x;
        
        
        didSomething = true;
    } else if (_ms == eDraggingInnerMidBtm) {
        _innerBtmLeftDragPos.y += delta.y;
        _innerTopRightDragPos.y -= delta.y;
        
        _outerBtmLeftDragPos.y += delta.y;
        _outerTopRightDragPos.y -= delta.y;

        didSomething = true;
    } else if (_ms == eDraggingInnerMidLeft) {
        _innerBtmLeftDragPos.x += delta.x;
        _innerTopRightDragPos.x -= delta.x;
        
        _outerBtmLeftDragPos.x += delta.x;
        _outerTopRightDragPos.x -= delta.x;
        didSomething = true;
    } else if (_ms == eDraggingOuterBottomLeft) {
        _outerBtmLeftDragPos.x += delta.x;
        _outerBtmLeftDragPos.y += delta.y;
        _outerTopRightDragPos.x -= multiplier * delta.x;
        _outerTopRightDragPos.y -= multiplier * delta.y;
        didSomething = true;
    } else if (_ms == eDraggingOuterTopLeft) {
        _outerBtmLeftDragPos.x += delta.x;
        if (!_controlDown) {
            _outerBtmLeftDragPos.y -= delta.y;
        }
        _outerTopRightDragPos.y += delta.y;
        _outerTopRightDragPos.x -= multiplier * delta.x;
        didSomething = true;
    } else if (_ms == eDraggingOuterTopRight) {
        if (!_controlDown) {
            _outerBtmLeftDragPos.x -= delta.x;
            _outerBtmLeftDragPos.y -= delta.y;
        }
        _outerTopRightDragPos.y +=  delta.y;
        _outerTopRightDragPos.x +=  delta.x;
        didSomething = true;
    } else if (_ms == eDraggingOuterBottomRight) {
        _outerTopRightDragPos.y -= multiplier * delta.y;
        _outerTopRightDragPos.x +=  delta.x;
        _outerBtmLeftDragPos.y += delta.y;
        if (!_controlDown) {
            _outerBtmLeftDragPos.x -= delta.x;
        }
        didSomething = true;
    } else if (_ms == eDraggingOuterMidTop) {
        if (!_controlDown) {
            _outerBtmLeftDragPos.y -= delta.y;
        }
        _outerTopRightDragPos.y += delta.y;
        didSomething = true;
    } else if (_ms == eDraggingOuterMidRight) {
        _outerTopRightDragPos.x +=  delta.x;
        if (!_controlDown) {
            _outerBtmLeftDragPos.x -= delta.x;
        }
        didSomething = true;
    } else if (_ms == eDraggingOuterMidBtm) {
        _outerBtmLeftDragPos.y += delta.y;
        _outerTopRightDragPos.y -= multiplier * delta.y;
        didSomething = true;
    } else if (_ms == eDraggingOuterMidLeft) {
        _outerBtmLeftDragPos.x += delta.x;
        _outerTopRightDragPos.x -= multiplier * delta.x;
        didSomething = true;
    } else if (_ms == eDraggingCenter) {
        _centerDragPos.x += delta.x;
        _centerDragPos.y += delta.y;
        _innerBtmLeftDragPos.x += delta.x;
        _innerBtmLeftDragPos.y += delta.y;
        _innerTopRightDragPos.x += delta.x;
        _innerTopRightDragPos.y += delta.y;
        _outerBtmLeftDragPos.x += delta.x;
        _outerBtmLeftDragPos.y += delta.y;
        _outerTopRightDragPos.x += delta.x;
        _outerTopRightDragPos.y += delta.y;
        didSomething = true;
    }
    
    
    if (isDraggingOuterPoint()) {
        ///clamp the outer rect to the inner rect
        OfxPointD innerBtmLeft,innerTopRight;
        _innerBtmLeft->getValue(innerBtmLeft.x, innerBtmLeft.y);
        _innerTopRight->getValue(innerTopRight.x, innerTopRight.y);
        
        ///convert to absolute coords.
        innerBtmLeft.x += center.x;
        innerBtmLeft.y += center.y;
        innerTopRight.x += center.x;
        innerTopRight.y += center.y;

        if (_outerBtmLeftDragPos.x > innerBtmLeft.x) {
            _outerBtmLeftDragPos.x = innerBtmLeft.x;
        }

        if (_outerBtmLeftDragPos.y > innerBtmLeft.y) {
            _outerBtmLeftDragPos.y = innerBtmLeft.y;
        }
        
        if (_outerTopRightDragPos.x < _innerTopRightDragPos.x) {
            _outerTopRightDragPos.x = _innerTopRightDragPos.x;
        }
        if (_outerTopRightDragPos.y < _innerTopRightDragPos.y) {
            _outerTopRightDragPos.y = _innerTopRightDragPos.y;
        }
        
        if (_controlDown) {
            if (_outerTopRightDragPos.x < innerTopRight.x) {
                _outerTopRightDragPos.x = innerTopRight.x;
            }
            if (_outerTopRightDragPos.y < innerTopRight.y) {
                _outerTopRightDragPos.y = innerTopRight.y;
            }
        }
    }
    
    if (isDraggingInnerPoint()) {
        ///clamp to the center point
        if (_innerBtmLeftDragPos.x > center.x) {
            double diffX = _innerBtmLeftDragPos.x - center.x;
            _innerBtmLeftDragPos.x = center.x;
            _outerBtmLeftDragPos.x -= diffX;
            _outerTopRightDragPos.x += multiplier * diffX;
            _innerTopRightDragPos.x += multiplier * diffX;
        }
        if (_innerBtmLeftDragPos.y > center.y) {
            double diffY = _innerBtmLeftDragPos.y - center.y;
            _innerBtmLeftDragPos.y = center.y;
            _outerBtmLeftDragPos.y -= diffY;
            _outerTopRightDragPos.y += multiplier * diffY;
            _innerTopRightDragPos.y += multiplier * diffY;
        }
        if (_innerTopRightDragPos.x < center.x) {
            double diffX = _innerTopRightDragPos.x - center.x;
            _innerTopRightDragPos.x = center.x;
            _outerTopRightDragPos.x += diffX;
            _outerBtmLeftDragPos.x -= multiplier * diffX;
            _innerBtmLeftDragPos.x -= multiplier * diffX;
        }
        if (_innerTopRightDragPos.y < center.y) {
            double diffY = _innerTopRightDragPos.y - center.y;
            _innerTopRightDragPos.y = center.y;
            _outerTopRightDragPos.y -= diffY;
            _outerBtmLeftDragPos.y -= multiplier * diffY;
            _innerBtmLeftDragPos.y -= multiplier * diffY;
        }
//
//        if (_controlDown) {
//            if ((_innerBtmLeftDragPos.x + _innerTopRightDragPos.x) < center.x) {
//                double diffX = center.x - _innerBtmLeftDragPos.x - _innerTopRightDragPos.x;
//                _innerTopRightDragPos.x = center.x - _innerBtmLeftDragPos.x;
//                _outerTopRightDragPos.x +=  diffX;
//            }
//            
//            if ((_innerBtmLeftDragPos.y + _innerTopRightDragPos.y) < center.y) {
//                double diffY = center.y - _innerBtmLeftDragPos.y - _innerTopRightDragPos.y;
//                _innerTopRightDragPos.y = center.y - _innerBtmLeftDragPos.y;
//                _outerTopRightDragPos.y += diffY;
//            }
//            
//        }
    }
    
    ///forbid 0 pixels wide rectangles
    if (_innerTopRightDragPos.x <= _innerBtmLeftDragPos.x) {
        _innerBtmLeftDragPos.x = (_innerTopRightDragPos.x + _innerBtmLeftDragPos.x)/2;
        _innerTopRightDragPos.x = _innerBtmLeftDragPos.x + 1;
    }
    if (_innerTopRightDragPos.y <= _innerBtmLeftDragPos.y) {
        _innerBtmLeftDragPos.y = (_innerTopRightDragPos.y + _innerBtmLeftDragPos.y)/2;
        _innerTopRightDragPos.y = _innerBtmLeftDragPos.y + 1;
    }
    if (_outerTopRightDragPos.x <= _outerBtmLeftDragPos.x) {
        _outerBtmLeftDragPos.x = (_outerTopRightDragPos.x + _outerBtmLeftDragPos.x)/2;
        _outerTopRightDragPos.x = _outerBtmLeftDragPos.x + 1;
    }
    if (_outerTopRightDragPos.y <= _outerBtmLeftDragPos.y) {
        _outerBtmLeftDragPos.y = (_outerTopRightDragPos.y + _outerBtmLeftDragPos.y)/2;
        _outerTopRightDragPos.y = _outerBtmLeftDragPos.y + 1;
    }

    
    ///repaint if we toggled off a hovered handle
    if (lastStateWasHovered && !didSomething) {
        didSomething = true;
    }
    
    _lastMousePos = args.penPosition;
    return didSomething;
}

bool TrackerRegionInteract::penDown(const OFX::PenArgs &args)
{
    bool didSomething = false;
    
    double selectionTol = 8. * args.pixelScale.x;
    
    OfxPointD innerTopRight,outerTopRight,innerBtmLeft,outerBtmLeft,center;
    _innerTopRight->getValueAtTime(args.time, innerTopRight.x, innerTopRight.y);
    _innerBtmLeft->getValueAtTime(args.time, innerBtmLeft.x, innerBtmLeft.y);
    _outerTopRight->getValueAtTime(args.time, outerTopRight.x, outerTopRight.y);
    _outerBtmLeft->getValueAtTime(args.time, outerBtmLeft.x, outerBtmLeft.y);
    _center->getValueAtTime(args.time, center.x, center.y);
    
    
    ///innerBtmLeft and outerBtmLeft are relative to the center, make them absolute
    innerBtmLeft.x += center.x;
    innerBtmLeft.y += center.y;
    innerTopRight.x += center.x;
    innerTopRight.y += center.y;

    outerBtmLeft.x += center.x;
    outerBtmLeft.y += center.y;
    outerTopRight.x += center.x;
    outerTopRight.y += center.y;

    if (isNearbyCenter(args.penPosition, selectionTol, center)) {
        _ms = eDraggingCenter;
        didSomething = true;
    } else if (isNearbyBtmLeft(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
        _ms = eDraggingInnerBottomLeft;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
        _ms = eDraggingInnerBottomRight;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
        _ms = eDraggingInnerTopRight;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
        _ms = eDraggingInnerTopLeft;
        didSomething = true;
    } else if (isNearbyMidTop(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
        _ms = eDraggingInnerMidTop;
        didSomething = true;
    } else if (isNearbyMidRight(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
        _ms = eDraggingInnerMidRight;
        didSomething = true;
    } else if (isNearbyMidBtm(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
        _ms = eDraggingInnerMidBtm;
        didSomething = true;
    } else if (isNearbyMidLeft(args.penPosition, selectionTol, innerBtmLeft, innerTopRight)) {
        _ms = eDraggingInnerMidLeft;
        didSomething = true;
    } else if (isNearbyBtmLeft(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
        _ms = eDraggingOuterBottomLeft;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
        _ms = eDraggingOuterBottomRight;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
        _ms = eDraggingOuterTopRight;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
        _ms = eDraggingOuterTopLeft;
        didSomething = true;
    } else if (isNearbyMidTop(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
        _ms = eDraggingOuterMidTop;
        didSomething = true;
    } else if (isNearbyMidRight(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
        _ms = eDraggingOuterMidRight;
        didSomething = true;
    } else if (isNearbyMidBtm(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
        _ms = eDraggingOuterMidBtm;
        didSomething = true;
    } else if (isNearbyMidLeft(args.penPosition, selectionTol, outerBtmLeft, outerTopRight)) {
        _ms = eDraggingOuterMidLeft;
        didSomething = true;
    } else {
        _ms = eIdle;
    }
 
    
    ///Keep the points in absolute coordinates
    _innerBtmLeftDragPos = innerBtmLeft;
    _innerTopRightDragPos = innerTopRight;
    _outerBtmLeftDragPos = outerBtmLeft;
    _outerTopRightDragPos = outerTopRight;
    _centerDragPos = center;
    
    _lastMousePos = args.penPosition;
    return didSomething;
}

bool TrackerRegionInteract::isDraggingInnerPoint() const
{
    return _ms == eDraggingInnerTopLeft ||
            _ms == eDraggingInnerTopRight ||
            _ms == eDraggingInnerBottomLeft ||
            _ms == eDraggingInnerBottomRight ||
            _ms == eDraggingInnerMidTop ||
            _ms == eDraggingInnerMidRight ||
            _ms == eDraggingInnerMidBtm ||
            _ms == eDraggingInnerMidLeft;
}

bool TrackerRegionInteract::isDraggingOuterPoint() const
{
    return _ms == eDraggingOuterTopLeft ||
    _ms == eDraggingOuterTopRight ||
    _ms == eDraggingOuterBottomLeft ||
    _ms == eDraggingOuterBottomRight ||
    _ms == eDraggingOuterMidTop ||
    _ms == eDraggingOuterMidRight ||
    _ms == eDraggingOuterMidBtm ||
    _ms == eDraggingOuterMidLeft;
}

bool TrackerRegionInteract::penUp(const OFX::PenArgs &args)
{
    if (_ms == eIdle) {
        return false;
    }
    
    OfxPointD center;
    if (_ms != eDraggingCenter) {
        _center->getValue(center.x, center.y);
    } else {
        center = _centerDragPos;
    }
    {
        OfxPointD btmLeft;
        btmLeft.x = _innerBtmLeftDragPos.x - center.x;
        btmLeft.y = _innerBtmLeftDragPos.y - center.y;
        
        _innerBtmLeft->setValue(btmLeft.x, btmLeft.y);

        OfxPointD topRight;
        topRight.x = _innerTopRightDragPos.x - center.x;
        topRight.y = _innerTopRightDragPos.y - center.y;

        _innerTopRight->setValue(topRight.x, topRight.y);
    }
    {
        OfxPointD btmLeft;
        btmLeft.x = _outerBtmLeftDragPos.x - center.x;
        btmLeft.y = _outerBtmLeftDragPos.y - center.y;
        _outerBtmLeft->setValue(btmLeft.x, btmLeft.y);

        OfxPointD topRight;
        topRight.x = _outerTopRightDragPos.x - center.x;
        topRight.y = _outerTopRightDragPos.y - center.y;
        _outerTopRight->setValue(topRight.x, topRight.y);
    }
    
    if (_ms == eDraggingCenter) {
        _center->setValueAtTime(args.time,_centerDragPos.x, _centerDragPos.y);
    }

    _ms = eIdle;
    return true;
}

bool TrackerRegionInteract::keyDown(const OFX::KeyArgs &args)
{
    if (args.keySymbol == kOfxKey_Control_L || args.keySymbol == kOfxKey_Control_R) {
        _controlDown = true;
        return true;
    }
    return false;
}

bool TrackerRegionInteract::keyUp(const OFX::KeyArgs &args)
{
    if (args.keySymbol == kOfxKey_Control_L || args.keySymbol == kOfxKey_Control_R) {
        _controlDown = false;
        return true;
    }
    return false;
}