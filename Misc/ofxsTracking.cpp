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
, _innerSize(0)
, _outterBtmLeft(0)
, _outterSize(0)
{
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA);

    
    _center = fetchDouble2DParam(kTrackCenterPointParamName);
    _innerBtmLeft = fetchDouble2DParam(kTrackPatternBoxPositionParamName);
    _innerSize = fetchDouble2DParam(kTrackSearchBoxSizeParamName);
    _outterBtmLeft = fetchDouble2DParam(kTrackSearchBoxPositionParamName);
    _outterSize = fetchDouble2DParam(kTrackSearchBoxSizeParamName);
    
    assert(_center && _innerSize && _innerBtmLeft && _outterSize && _outterBtmLeft);
}

bool GenericTrackerPlugin::isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime)
{
    identityClip = srcClip_;
    identityTime = args.time;
    return true;
}



void genericTrackerDescribe(OFX::ImageEffectDescriptor &desc)
{
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);
    
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);
    
    
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    
    ///We do temporal clip access
    desc.setTemporalClipAccess(true);
    
    desc.setRenderTwiceAlways(false);
    
    desc.setSupportsMultipleClipPARs(false);
    
    desc.setRenderThreadSafety(eRenderFullySafe);
    
    
    // in order to support tiles, the plugin must implement the getRegionOfInterest function
    desc.setSupportsTiles(true);
    
    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(true);
    

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
    center->setDoubleType(eDoubleTypeXYAbsolute);
    center->setDefaultCoordinateSystem(eCoordinatesNormalised);
    center->setDefault(0.5, 0.5);
    center->setHint("The center point to track");
    page->addChild(*center);
    
    
    OFX::Double2DParamDescriptor* innerBtmLeft = desc.defineDouble2DParam(kTrackPatternBoxPositionParamName);
    innerBtmLeft->setLabels(kTrackPatternBoxPositionParamLabel, kTrackPatternBoxPositionParamLabel, kTrackPatternBoxPositionParamLabel);
    innerBtmLeft->setDefaultCoordinateSystem(eCoordinatesNormalised);
    innerBtmLeft->setDoubleType(eDoubleTypeXYAbsolute);
    innerBtmLeft->setDefault(-0.05,-0.05);
    innerBtmLeft->setIsSecret(true);
    innerBtmLeft->setHint("The bottom left corner of the inner pattern box. The coordinates are relative to the center point.");
    page->addChild(*innerBtmLeft);
    
    OFX::Double2DParamDescriptor* innerSize = desc.defineDouble2DParam(kTrackPatternBoxSizeParamName);
    innerSize->setLabels(kTrackPatternBoxSizeParamLabel, kTrackPatternBoxSizeParamLabel, kTrackPatternBoxSizeParamLabel);
    innerSize->setDefaultCoordinateSystem(eCoordinatesNormalised);
    innerSize->setDoubleType(eDoubleTypeXYAbsolute);
    innerSize->setDefault(0.1, 0.1);
    innerSize->setIsSecret(true);
    innerSize->setDimensionLabels("width", "height");
    innerSize->setHint("This is the width and height of the pattern box.");
    page->addChild(*innerSize);
    
    OFX::Double2DParamDescriptor* outterBtmLeft = desc.defineDouble2DParam(kTrackSearchBoxPositionParamName);
    outterBtmLeft->setLabels(kTrackSearchBoxPositionParamLabel, kTrackSearchBoxPositionParamLabel, kTrackSearchBoxPositionParamLabel);
    outterBtmLeft->setDefaultCoordinateSystem(eCoordinatesNormalised);
    outterBtmLeft->setDoubleType(eDoubleTypeXYAbsolute);
    outterBtmLeft->setDefault(-0.1,-0.1);
    outterBtmLeft->setIsSecret(true);
    outterBtmLeft->setHint("The bottom left corner of the search area. The coordinates are relative to the center point.");
    page->addChild(*outterBtmLeft);
    
    OFX::Double2DParamDescriptor* outterSize = desc.defineDouble2DParam(kTrackSearchBoxSizeParamName);
    outterSize->setLabels(kTrackSearchBoxSizeParamLabel, kTrackSearchBoxSizeParamLabel, kTrackSearchBoxSizeParamLabel);
    outterSize->setDefaultCoordinateSystem(eCoordinatesNormalised);
    outterSize->setDoubleType(eDoubleTypeXYAbsolute);
    outterSize->setDefault(0.2, 0.2);
    outterSize->setIsSecret(true);
    outterSize->setDimensionLabels("width", "height");
    outterSize->setHint("This is the width and height of the search area.");
    page->addChild(*outterSize);

    OFX::StringParamDescriptor* name = desc.defineStringParam(kOfxParamStringEffectInstanceLabel);
    name->setLabels(kOfxParamStringEffectInstanceLabel, kOfxParamStringEffectInstanceLabel, kOfxParamStringEffectInstanceLabel);
    name->setIsSecret(true);
    name->setHint("The name of the instance of the plug-in. This is used internally by Natron to set the track instance name.");
    name->setDefault("Track");
    name->setIsPersistant(false);
    name->setEnabled(false);
    name->setEvaluateOnChange(false);
    page->addChild(*name);
    
}

//////////////////// INTERACT ////////////////////

bool TrackerRegionInteract::isNearbyTopLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD topLeft;
    topLeft.x = btmLeft.x;
    topLeft.y = btmLeft.y + size.y;
    if (pos.x >= (topLeft.x - tolerance) && pos.x <= (topLeft.x + tolerance) &&
        pos.y >= (topLeft.y - tolerance) && pos.y <= (topLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool TrackerRegionInteract::isNearbyTopRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD topRight;
    topRight.x = btmLeft.x + size.x;
    topRight.y = btmLeft.y + size.y;
    if (pos.x >= (topRight.x - tolerance) && pos.x <= (topRight.x + tolerance) &&
        pos.y >= (topRight.y - tolerance) && pos.y <= (topRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool TrackerRegionInteract::isNearbyBtmLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    if (pos.x >= (btmLeft.x - tolerance) && pos.x <= (btmLeft.x + tolerance) &&
        pos.y >= (btmLeft.y - tolerance) && pos.y <= (btmLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool TrackerRegionInteract::isNearbyBtmRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD btmRight;
    btmRight.x = btmLeft.x + size.x;
    btmRight.y = btmLeft.y ;
    if (pos.x >= (btmRight.x - tolerance) && pos.x <= (btmRight.x + tolerance) &&
        pos.y >= (btmRight.y - tolerance) && pos.y <= (btmRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool TrackerRegionInteract::isNearbyMidTop(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD midTop;
    midTop.x = btmLeft.x + size.x / 2.;
    midTop.y = btmLeft.y + size.y;
    if (pos.x >= (midTop.x - tolerance) && pos.x <= (midTop.x + tolerance) &&
        pos.y >= (midTop.y - tolerance) && pos.y <= (midTop.y + tolerance)) {
        return true;
    } else {
        return false;
    }
    
}

bool TrackerRegionInteract::isNearbyMidRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD midRight;
    midRight.x = btmLeft.x + size.x;
    midRight.y = btmLeft.y + size.y / 2.;
    if (pos.x >= (midRight.x - tolerance) && pos.x <= (midRight.x + tolerance) &&
        pos.y >= (midRight.y - tolerance) && pos.y <= (midRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool TrackerRegionInteract::isNearbyMidLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD midLeft;
    midLeft.x = btmLeft.x;
    midLeft.y = btmLeft.y + size.y /2.;
    if (pos.x >= (midLeft.x - tolerance) && pos.x <= (midLeft.x + tolerance) &&
        pos.y >= (midLeft.y - tolerance) && pos.y <= (midLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }
    
}

bool TrackerRegionInteract::isNearbyMidBtm(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD midBtm;
    midBtm.x = btmLeft.x + size.x / 2.;
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
    OfxPointD innerBtmLeft,innerSize,outterBtmLeft,outterSize,center;
    
    if (_ms != eIdle) {
        innerBtmLeft = _innerBtmLeftDragPos;
        innerSize = _innerSizeDrag;
        outterBtmLeft = _outterBtmLeftDragPos;
        outterSize = _outterSizeDrag;
        center = _centerDragPos;

    } else {
        _center->getValueAtTime(args.time, center.x, center.y);

        _innerBtmLeft->getValueAtTime(args.time, innerBtmLeft.x, innerBtmLeft.y);
        _innerSize->getValueAtTime(args.time, innerSize.x, innerSize.y);
        
        ///innerBtmLeft is relative to the center, make it absolute
        innerBtmLeft.x += center.x;
        innerBtmLeft.y += center.y;

        _outterBtmLeft->getValueAtTime(args.time, outterBtmLeft.x, outterBtmLeft.y);
        _outterSize->getValueAtTime(args.time, outterSize.x, outterSize.y);

        
        ///outterBtmLeft is relative to the center, make it absolute
        outterBtmLeft.x += center.x;
        outterBtmLeft.y += center.y;
    }
    
    
    ///Compute all other points positions given the 5 parameters retrieved above
    OfxPointD innerTopLeft,innerMidTop,innerTopRight,innerMidRight,innerBtmRight,innerMidBtm,innerMidLeft;
    OfxPointD outterTopLeft,outterMidTop,outterTopRight,outterMidRight,outterBtmRight,outterMidBtm,outterMidLeft;
    innerTopLeft.x = innerBtmLeft.x;
    innerTopLeft.y = innerBtmLeft.y + innerSize.y;
    innerMidTop.x = innerBtmLeft.x + innerSize.x / 2.;
    innerMidTop.y = innerTopLeft.y;
    innerTopRight.x = innerBtmLeft.x + innerSize.x;
    innerTopRight.y = innerTopLeft.y;
    innerMidRight.x = innerTopRight.x;
    innerMidRight.y = innerBtmLeft.y + innerSize.y / 2.;
    innerBtmRight.x = innerMidRight.x;
    innerBtmRight.y = innerBtmLeft.y;
    innerMidBtm.x = innerMidTop.x;
    innerMidBtm.y = innerBtmLeft.y;
    innerMidLeft.x = innerBtmLeft.x;
    innerMidLeft.y = innerMidRight.y;
    
    outterTopLeft.x = outterBtmLeft.x;
    outterTopLeft.y = outterBtmLeft.y + outterSize.y;
    outterMidTop.x = outterBtmLeft.x + outterSize.x / 2.;
    outterMidTop.y = outterTopLeft.y;
    outterTopRight.x = outterBtmLeft.x + outterSize.x;
    outterTopRight.y = outterTopLeft.y;
    outterMidRight.x = outterTopRight.x;
    outterMidRight.y = outterBtmLeft.y + outterSize.y / 2.;
    outterBtmRight.x = outterMidRight.x;
    outterBtmRight.y = outterBtmLeft.y;
    outterMidBtm.x = outterMidTop.x;
    outterMidBtm.y = outterBtmLeft.y;
    outterMidLeft.x = outterBtmLeft.x;
    outterMidLeft.y = outterMidRight.y;


    
    glColor4f(0.9, 0.9, 0.9, 1.);
    glBegin(GL_LINE_STRIP);
    glVertex2d(innerBtmLeft.x, innerBtmLeft.y);
    glVertex2d(innerTopLeft.x, innerTopLeft.y);
    glVertex2d(innerTopRight.x, innerTopRight.y);
    glVertex2d(innerBtmRight.x, innerBtmRight.y);
    glVertex2d(innerBtmLeft.x, innerBtmLeft.y);
    glEnd();
    
    glBegin(GL_LINE_STRIP);
    glVertex2d(outterBtmLeft.x, outterBtmLeft.y);
    glVertex2d(outterTopLeft.x, outterTopLeft.y);
    glVertex2d(outterTopRight.x, outterTopRight.y);
    glVertex2d(outterBtmRight.x, outterBtmRight.y);
    glVertex2d(outterBtmLeft.x, outterBtmLeft.y);
    glEnd();
    
    glPointSize(6);
    glBegin(GL_POINTS);
    
    //////DRAWING INNER POINTS
    if (_ds == eHoveringInnerBottomLeft || _ms == eDraggingInnerBottomLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    
    glVertex2d(innerBtmLeft.x, innerBtmLeft.y);
    
    if (_ds == eHoveringInnerMidLeft || _ms == eDraggingInnerMidLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(innerMidLeft.x, innerMidLeft.y);
    
    if (_ds == eHoveringInnerTopLeft || _ms == eDraggingInnerTopLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(innerTopLeft.x, innerTopLeft.y);
    
    if (_ds == eHoveringInnerMidTop || _ms == eDraggingInnerMidTop) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(innerMidTop.x, innerMidTop.y);
    
    if (_ds == eHoveringInnerTopRight || _ms == eDraggingInnerTopRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(innerTopRight.x, innerTopRight.y);
    
    if (_ds == eHoveringInnerMidRight || _ms == eDraggingInnerMidRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(innerMidRight.x, innerMidRight.y);
    
    if (_ds == eHoveringInnerBottomRight || _ms == eDraggingInnerBottomRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(innerBtmRight.x, innerBtmRight.y);
    
    if (_ds == eHoveringInnerMidBtm || _ms == eDraggingInnerMidBtm) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(innerMidBtm.x, innerMidBtm.y);
    
    //////DRAWING OUTTER POINTS
    
    if (_ds == eHoveringOutterBottomLeft || _ms == eDraggingOutterBottomLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    
    glVertex2d(outterBtmLeft.x, outterBtmLeft.y);
    
    if (_ds == eHoveringOutterMidLeft || _ms == eDraggingOutterMidLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(outterMidLeft.x, outterMidLeft.y);
    
    if (_ds == eHoveringOutterTopLeft || _ms == eDraggingOutterTopLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(outterTopLeft.x, outterTopLeft.y);
    
    if (_ds == eHoveringOutterMidTop || _ms == eDraggingOutterMidTop) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(outterMidTop.x, outterMidTop.y);
    
    if (_ds == eHoveringOutterTopRight || _ms == eDraggingOutterTopRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(outterTopRight.x, outterTopRight.y);
    
    if (_ds == eHoveringOutterMidRight || _ms == eDraggingOutterMidRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(outterMidRight.x, outterMidRight.y);
    
    if (_ds == eHoveringOutterBottomRight || _ms == eDraggingOutterBottomRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(outterBtmRight.x, outterBtmRight.y);
    
    if (_ds == eHoveringOutterMidBtm || _ms == eDraggingOutterMidBtm) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(outterMidBtm.x, outterMidBtm.y);
    
    ///draw center
    if (_ds == eHoveringCenter || _ms == eDraggingCenter) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(center.x,center.y);
    glEnd();
    glPointSize(1);

    
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
    
    double selectionTol = 15. * args.pixelScale.x;
    
    OfxPointD innerSize,outterSize,innerBtmLeft,outterBtmLeft,center;
    _innerSize->getValueAtTime(args.time, innerSize.x, innerSize.y);
    _outterSize->getValueAtTime(args.time, outterSize.x, outterSize.y);
    _innerBtmLeft->getValueAtTime(args.time, innerBtmLeft.x, innerBtmLeft.y);
    _outterBtmLeft->getValueAtTime(args.time, outterBtmLeft.x, outterBtmLeft.y);
    _center->getValueAtTime(args.time, center.x, center.y);
    ///innerBtmLeft and outterBtmLeft are relative to the center, make them absolute
    innerBtmLeft.x += center.x;
    innerBtmLeft.y += center.y;
    
    outterBtmLeft.x += center.x;
    outterBtmLeft.y += center.y;
    
    bool lastStateWasHovered = _ds != eInactive;
    
    if (isNearbyBtmLeft(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ds = eHoveringInnerBottomLeft;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ds = eHoveringInnerBottomRight;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ds = eHoveringInnerTopRight;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ds = eHoveringInnerTopLeft;
        didSomething = true;
    } else if (isNearbyMidTop(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ds = eHoveringInnerMidTop;
        didSomething = true;
    } else if (isNearbyMidRight(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ds = eHoveringInnerMidRight;
        didSomething = true;
    } else if (isNearbyMidBtm(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ds = eHoveringInnerMidBtm;
        didSomething = true;
    } else if (isNearbyMidLeft(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ds = eHoveringInnerMidLeft;
        didSomething = true;
    } else if (isNearbyCenter(args.penPosition, selectionTol, center)) {
        _ds = eHoveringCenter;
        didSomething = true;
    } else if (isNearbyBtmLeft(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ds = eHoveringOutterBottomLeft;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ds = eHoveringOutterBottomRight;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ds = eHoveringOutterTopRight;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ds = eHoveringOutterTopLeft;
        didSomething = true;
    } else if (isNearbyMidTop(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ds = eHoveringOutterMidTop;
        didSomething = true;
    } else if (isNearbyMidRight(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ds = eHoveringOutterMidRight;
        didSomething = true;
    } else if (isNearbyMidBtm(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ds = eHoveringOutterMidBtm;
        didSomething = true;
    } else if (isNearbyMidLeft(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ds = eHoveringOutterMidLeft;
        didSomething = true;
    } else {
        _ds = eInactive;
    }
    
    int multiplier = _controlDown ? 1 : 2;
    
    if (_ms == eDraggingInnerBottomLeft) {
        _innerBtmLeftDragPos.x += delta.x;
        _innerBtmLeftDragPos.y += delta.y;
        _innerSizeDrag.x -= multiplier * delta.x;
        _innerSizeDrag.y -= multiplier * delta.y;
        ///also move the outter rect
        _outterBtmLeftDragPos.x += delta.x;
        _outterBtmLeftDragPos.y += delta.y;
        _outterSizeDrag.x -= multiplier * delta.x;
        _outterSizeDrag.y -= multiplier * delta.y;
        didSomething = true;
    } else if (_ms == eDraggingInnerTopLeft) {
        _innerBtmLeftDragPos.x += delta.x;
        if (!_controlDown) {
            _innerBtmLeftDragPos.y -= delta.y;
        }
        _innerSizeDrag.y += multiplier * delta.y;
        _innerSizeDrag.x -= multiplier * delta.x;
        
        _outterBtmLeftDragPos.x += delta.x;
        if (!_controlDown) {
            _outterBtmLeftDragPos.y -= delta.y;
        }
        _outterSizeDrag.y += multiplier * delta.y;
        _outterSizeDrag.x -= multiplier * delta.x;
        didSomething = true;
    } else if (_ms == eDraggingInnerTopRight) {
        if (!_controlDown) {
            _innerBtmLeftDragPos.x -= delta.x;
            _innerBtmLeftDragPos.y -= delta.y;
        }
        _innerSizeDrag.y += multiplier * delta.y;
        _innerSizeDrag.x += multiplier * delta.x;
        
        if (!_controlDown) {
            _outterBtmLeftDragPos.x -= delta.x;
            _outterBtmLeftDragPos.y -= delta.y;
        }
        _outterSizeDrag.y += multiplier * delta.y;
        _outterSizeDrag.x += multiplier * delta.x;
        didSomething = true;
    } else if (_ms == eDraggingInnerBottomRight) {
        _innerSizeDrag.y -= multiplier * delta.y;
        _innerSizeDrag.x += multiplier * delta.x;
        _innerBtmLeftDragPos.y += delta.y;
        if (!_controlDown) {
            _innerBtmLeftDragPos.x -= delta.x;
        }
        
        _outterSizeDrag.y -= multiplier * delta.y;
        _outterSizeDrag.x += multiplier * delta.x;
        _outterBtmLeftDragPos.y += delta.y;
        if (!_controlDown) {
            _outterBtmLeftDragPos.x -= delta.x;
        }

        didSomething = true;
    } else if (_ms == eDraggingInnerMidTop) {
        if (!_controlDown) {
            _innerBtmLeftDragPos.y -= delta.y;
            _outterBtmLeftDragPos.y -= delta.y;
        }
        _innerSizeDrag.y += multiplier * delta.y;
        _outterSizeDrag.y += multiplier * delta.y;

        didSomething = true;
    } else if (_ms == eDraggingInnerMidRight) {
        _innerSizeDrag.x += multiplier * delta.x;
        _outterSizeDrag.x += multiplier * delta.x;
        if (!_controlDown) {
            _innerBtmLeftDragPos.x -= delta.x;
            _outterBtmLeftDragPos.x -= delta.x;
        }
        
        didSomething = true;
    } else if (_ms == eDraggingInnerMidBtm) {
        _innerBtmLeftDragPos.y += delta.y;
        _innerSizeDrag.y -= multiplier * delta.y;
        
        _outterBtmLeftDragPos.y += delta.y;
        _outterSizeDrag.y -= multiplier * delta.y;

        didSomething = true;
    } else if (_ms == eDraggingInnerMidLeft) {
        _innerBtmLeftDragPos.x += delta.x;
        _innerSizeDrag.x -= multiplier * delta.x;
        
        _outterBtmLeftDragPos.x += delta.x;
        _outterSizeDrag.x -= multiplier * delta.x;
        didSomething = true;
    } else if (_ms == eDraggingOutterBottomLeft) {
        _outterBtmLeftDragPos.x += delta.x;
        _outterBtmLeftDragPos.y += delta.y;
        _outterSizeDrag.x -= multiplier * delta.x;
        _outterSizeDrag.y -= multiplier * delta.y;
        didSomething = true;
    } else if (_ms == eDraggingOutterTopLeft) {
        _outterBtmLeftDragPos.x += delta.x;
        if (!_controlDown) {
            _outterBtmLeftDragPos.y -= delta.y;
        }
        _outterSizeDrag.y += multiplier * delta.y;
        _outterSizeDrag.x -= multiplier * delta.x;
        didSomething = true;
    } else if (_ms == eDraggingOutterTopRight) {
        if (!_controlDown) {
            _outterBtmLeftDragPos.x -= delta.x;
            _outterBtmLeftDragPos.y -= delta.y;
        }
        _outterSizeDrag.y += multiplier * delta.y;
        _outterSizeDrag.x += multiplier * delta.x;
        didSomething = true;
    } else if (_ms == eDraggingOutterBottomRight) {
        _outterSizeDrag.y -= multiplier * delta.y;
        _outterSizeDrag.x += multiplier * delta.x;
        _outterBtmLeftDragPos.y += delta.y;
        if (!_controlDown) {
            _outterBtmLeftDragPos.x -= delta.x;
        }
        didSomething = true;
    } else if (_ms == eDraggingOutterMidTop) {
        if (!_controlDown) {
            _outterBtmLeftDragPos.y -= delta.y;
        }
        _outterSizeDrag.y += multiplier * delta.y;
        didSomething = true;
    } else if (_ms == eDraggingOutterMidRight) {
        _outterSizeDrag.x += multiplier * delta.x;
        if (!_controlDown) {
            _outterBtmLeftDragPos.x -= delta.x;
        }
        didSomething = true;
    } else if (_ms == eDraggingOutterMidBtm) {
        _outterBtmLeftDragPos.y += delta.y;
        _outterSizeDrag.y -= multiplier * delta.y;
        didSomething = true;
    } else if (_ms == eDraggingOutterMidLeft) {
        _outterBtmLeftDragPos.x += delta.x;
        _outterSizeDrag.x -= multiplier * delta.x;
        didSomething = true;
    } else if (_ms == eDraggingCenter) {
        _centerDragPos.x += delta.x;
        _centerDragPos.y += delta.y;
        _outterBtmLeftDragPos.x += delta.x;
        _outterBtmLeftDragPos.y += delta.y;
        _innerBtmLeftDragPos.x += delta.x;
        _innerBtmLeftDragPos.y += delta.y;
        didSomething = true;
    }
    
    
    if (isDraggingOutterPoint()) {
        ///clamp the outter rect to the inner rect
        OfxPointD innerBtmLeft,innerSize;
        _innerBtmLeft->getValue(innerBtmLeft.x, innerBtmLeft.y);
        _innerSize->getValue(innerSize.x, innerSize.y);
        
        ///convert to absolute coords.
        innerBtmLeft.x += center.x;
        innerBtmLeft.y += center.y;
        
        if (_outterBtmLeftDragPos.x > innerBtmLeft.x) {
            _outterSizeDrag.x += _outterBtmLeftDragPos.x - innerBtmLeft.x;
            _outterBtmLeftDragPos.x = innerBtmLeft.x;
        }

        if (_outterBtmLeftDragPos.y > innerBtmLeft.y) {
            _outterSizeDrag.y += _outterBtmLeftDragPos.y - innerBtmLeft.y;
            _outterBtmLeftDragPos.y = innerBtmLeft.y;
        }
        
        if (_outterSizeDrag.x < _innerSizeDrag.x) {
            _outterSizeDrag.x = _innerSizeDrag.x;
        }
        if (_outterSizeDrag.y < _innerSizeDrag.y) {
            _outterSizeDrag.y = _innerSizeDrag.y;
        }
        
        if (_controlDown) {
            if ((_outterBtmLeftDragPos.x + _outterSizeDrag.x) < (innerBtmLeft.x + innerSize.x)) {
                _outterSizeDrag.x = ((innerBtmLeft.x + innerSize.x - _outterBtmLeftDragPos.x));
            }
            if ((_outterBtmLeftDragPos.y + _outterSizeDrag.y) < (innerBtmLeft.y + innerSize.y)) {
                _outterSizeDrag.y = ((innerBtmLeft.y + innerSize.y - _outterBtmLeftDragPos.y));
            }
        }
    }
    
    if (isDraggingInnerPoint()) {
        ///clamp to the center point
        if (_innerBtmLeftDragPos.x > center.x) {
            double diffX = _innerBtmLeftDragPos.x - center.x;
            _innerBtmLeftDragPos.x = center.x;
            _outterBtmLeftDragPos.x -= diffX;
            _outterSizeDrag.x += multiplier * diffX;
            _innerSizeDrag.x += multiplier * diffX;
        }
        if (_innerBtmLeftDragPos.y > center.y) {
            double diffY = _innerBtmLeftDragPos.y - center.y;
            _innerBtmLeftDragPos.y = center.y;
            _outterBtmLeftDragPos.y -= diffY;
            _outterSizeDrag.y += multiplier * diffY;
            _innerSizeDrag.y += multiplier * diffY;
        }
        
        if (_controlDown) {
            if ((_innerBtmLeftDragPos.x + _innerSizeDrag.x) < center.x) {
                double diffX = center.x - _innerBtmLeftDragPos.x - _innerSizeDrag.x;
                _innerSizeDrag.x = center.x - _innerBtmLeftDragPos.x;
                _outterSizeDrag.x +=  diffX;
            }
            
            if ((_innerBtmLeftDragPos.y + _innerSizeDrag.y) < center.y) {
                double diffY = center.y - _innerBtmLeftDragPos.y - _innerSizeDrag.y;
                _innerSizeDrag.y = center.y - _innerBtmLeftDragPos.y;
                _outterSizeDrag.y += diffY;
            }
            
        }
    }
    
    ///forbid 0 pixels wide rectangles
    if (_innerSizeDrag.x < 1) {
        _innerSizeDrag.x = 1;
    }
    if (_innerSizeDrag.y < 1) {
        _innerSizeDrag.y = 1;
    }
    if (_outterSizeDrag.x < 1) {
        _outterSizeDrag.x = 1;
    }
    if (_outterSizeDrag.y < 1) {
        _outterSizeDrag.y = 1;
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
    
    double selectionTol = 15. * args.pixelScale.x;
    
    OfxPointD innerSize,outterSize,innerBtmLeft,outterBtmLeft,center;
    _innerSize->getValueAtTime(args.time, innerSize.x, innerSize.y);
    _innerBtmLeft->getValueAtTime(args.time, innerBtmLeft.x, innerBtmLeft.y);
    _outterSize->getValueAtTime(args.time, outterSize.x, outterSize.y);
    _outterBtmLeft->getValueAtTime(args.time, outterBtmLeft.x, outterBtmLeft.y);
    _center->getValueAtTime(args.time, center.x, center.y);
    
    
    ///innerBtmLeft and outterBtmLeft are relative to the center, make them absolute
    innerBtmLeft.x += center.x;
    innerBtmLeft.y += center.y;
    
    outterBtmLeft.x += center.x;
    outterBtmLeft.y += center.y;
    
    if (isNearbyBtmLeft(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ms = eDraggingInnerBottomLeft;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ms = eDraggingInnerBottomRight;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ms = eDraggingInnerTopRight;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ms = eDraggingInnerTopLeft;
        didSomething = true;
    } else if (isNearbyMidTop(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ms = eDraggingInnerMidTop;
        didSomething = true;
    } else if (isNearbyMidRight(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ms = eDraggingInnerMidRight;
        didSomething = true;
    } else if (isNearbyMidBtm(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ms = eDraggingInnerMidBtm;
        didSomething = true;
    } else if (isNearbyMidLeft(args.penPosition, selectionTol, innerSize, innerBtmLeft)) {
        _ms = eDraggingInnerMidLeft;
        didSomething = true;
    } else if (isNearbyBtmLeft(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ms = eDraggingOutterBottomLeft;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ms = eDraggingOutterBottomRight;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ms = eDraggingOutterTopRight;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ms = eDraggingOutterTopLeft;
        didSomething = true;
    } else if (isNearbyMidTop(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ms = eDraggingOutterMidTop;
        didSomething = true;
    } else if (isNearbyMidRight(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ms = eDraggingOutterMidRight;
        didSomething = true;
    } else if (isNearbyMidBtm(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ms = eDraggingOutterMidBtm;
        didSomething = true;
    } else if (isNearbyMidLeft(args.penPosition, selectionTol, outterSize, outterBtmLeft)) {
        _ms = eDraggingOutterMidLeft;
        didSomething = true;
    } else if (isNearbyCenter(args.penPosition, selectionTol, center)) {
        _ms = eDraggingCenter;
        didSomething = true;
    } else {
        _ms = eIdle;
    }
 
    
    ///Keep the points in absolute coordinates
    _innerBtmLeftDragPos = innerBtmLeft;
    _innerSizeDrag = innerSize;
    _outterBtmLeftDragPos = outterBtmLeft;
    _outterSizeDrag = outterSize;
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

bool TrackerRegionInteract::isDraggingOutterPoint() const
{
    return _ms == eDraggingOutterTopLeft ||
    _ms == eDraggingOutterTopRight ||
    _ms == eDraggingOutterBottomLeft ||
    _ms == eDraggingOutterBottomRight ||
    _ms == eDraggingOutterMidTop ||
    _ms == eDraggingOutterMidRight ||
    _ms == eDraggingOutterMidBtm ||
    _ms == eDraggingOutterMidLeft;
}

bool TrackerRegionInteract::penUp(const OFX::PenArgs &args)
{
    
    
    bool didSmthing = false;
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
        _innerSize->setValue(_innerSizeDrag.x, _innerSizeDrag.y);
    }
    {
        OfxPointD btmLeft;
        btmLeft.x = _outterBtmLeftDragPos.x - center.x;
        btmLeft.y = _outterBtmLeftDragPos.y - center.y;
        _outterBtmLeft->setValue(btmLeft.x, btmLeft.y);
        _outterSize->setValue(_outterSizeDrag.x, _outterSizeDrag.y);
        didSmthing = true;

    }
    
    if (_ms == eDraggingCenter) {
        _center->setValue(_centerDragPos.x, _centerDragPos.y);
    }
    
    _ms = eIdle;
    return didSmthing;
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