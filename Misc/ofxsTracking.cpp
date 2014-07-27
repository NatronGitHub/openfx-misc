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
#include <cmath>

#include "ofxsOGLTextRenderer.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#define POINT_SIZE 5
#define POINT_TOLERANCE 6
#define HANDLE_SIZE 6

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
    _innerBtmLeft = fetchDouble2DParam(kTrackPatternBoxBtmLeftParamName);
    _innerTopRight = fetchDouble2DParam(kTrackPatternBoxTopRightParamName);
    _outerBtmLeft = fetchDouble2DParam(kTrackSearchBoxBtmLeftParamName);
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
    ///Declare the name first so that in Natron it appears as the first column in the multi instance
    OFX::StringParamDescriptor* name = desc.defineStringParam(kTrackLabelParamName);
    name->setLabels(kTrackLabelParamLabel, kTrackLabelParamLabel, kTrackLabelParamLabel);
    name->setHint(kTrackLabelParamHint);
    name->setDefault(kTrackLabelParamDefault);
    ////name->setIsSecret(false); // it has to be user-editable
    ////name->setEnabled(true); // it has to be user-editable
    ////name->setIsPersistant(true); // it has to be saved with the instance parameters
    name->setEvaluateOnChange(false); // it is meaningless
    page->addChild(*name);

    
    OFX::Double2DParamDescriptor* center = desc.defineDouble2DParam(kTrackCenterPointParamName);
    center->setLabels(kTrackCenterPointParamLabel, kTrackCenterPointParamLabel, kTrackCenterPointParamLabel);
    center->setHint(kTrackCenterPointParamHint);
    center->setDoubleType(eDoubleTypeXYAbsolute);
    center->setDefaultCoordinateSystem(eCoordinatesNormalised);
    center->setDefault(0.5, 0.5);
    center->getPropertySet().propSetInt(kOfxParamPropPluginMayWrite, 1);
    page->addChild(*center);
    
    
    OFX::Double2DParamDescriptor* innerBtmLeft = desc.defineDouble2DParam(kTrackPatternBoxBtmLeftParamName);
    innerBtmLeft->setLabels(kTrackPatternBoxBtmLeftParamLabel, kTrackPatternBoxBtmLeftParamLabel, kTrackPatternBoxBtmLeftParamLabel);
    innerBtmLeft->setHint(kTrackPatternBoxBtmLeftParamHint);
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
    
    OFX::Double2DParamDescriptor* outerBtmLeft = desc.defineDouble2DParam(kTrackSearchBoxBtmLeftParamName);
    outerBtmLeft->setLabels(kTrackSearchBoxBtmLeftParamLabel, kTrackSearchBoxBtmLeftParamLabel, kTrackSearchBoxBtmLeftParamLabel);
    outerBtmLeft->setHint(kTrackSearchBoxBtmLeftParamHint);
    outerBtmLeft->setDoubleType(eDoubleTypeXY);
    outerBtmLeft->setDefaultCoordinateSystem(eCoordinatesCanonical);
    outerBtmLeft->setDefault(-25,-25);
    //outerBtmLeft->setIsSecret(true);
    outerBtmLeft->getPropertySet().propSetInt(kOfxParamPropPluginMayWrite, 1);
    page->addChild(*outerBtmLeft);
    
    OFX::Double2DParamDescriptor* outerTopRight = desc.defineDouble2DParam(kTrackSearchBoxTopRightParamName);
    outerTopRight->setLabels(kTrackSearchBoxTopRightParamLabel, kTrackSearchBoxTopRightParamLabel, kTrackSearchBoxTopRightParamLabel);
    outerTopRight->setHint(kTrackSearchBoxBtmLeftParamHint);
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


    
}

//////////////////// INTERACT ////////////////////

static bool isNearby(const OfxPointD& p, double x, double y, double tolerance, const OfxPointD& pscale)
{
    return std::fabs(p.x-x) <= tolerance*pscale.x &&  std::fabs(p.y-y) <= tolerance*pscale.y;
}



bool TrackerRegionInteract::draw(const OFX::DrawArgs &args)
{
    OfxPointD pscale;
    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;

    double xi1, xi2, yi1, yi2, xo1, xo2, yo1, yo2, xc, yc;

    if (_ms != eIdle) {
        xi1 = _innerBtmLeftDragPos.x;
        yi1 = _innerBtmLeftDragPos.y;
        xi2 = _innerTopRightDragPos.x;
        yi2 = _innerTopRightDragPos.y;
        xo1 = _outerBtmLeftDragPos.x;
        yo1 = _outerBtmLeftDragPos.y;
        xo2 = _outerTopRightDragPos.x;
        yo2 = _outerTopRightDragPos.y;
        xc = _centerDragPos.x;
        yc = _centerDragPos.y;
    } else {
        _innerBtmLeft->getValueAtTime( args.time, xi1, yi1);
        _innerTopRight->getValueAtTime(args.time, xi2, yi2);
        _outerBtmLeft->getValueAtTime( args.time, xo1, yo1);
        _outerTopRight->getValueAtTime(args.time, xo2, yo2);
        _center->getValueAtTime(args.time, xc, yc);
        ///innerBtmLeft and outerBtmLeft are relative to the center, make them absolute
        xi1 += xc;
        yi1 += yc;
        xi2 += xc;
        yi2 += yc;
        xo1 += xc;
        yo1 += yc;
        xo2 += xc;
        yo2 += yc;
    }
    
    
    glPushAttrib(GL_ALL_ATTRIB_BITS);

    //glDisable(GL_LINE_STIPPLE);
    glEnable(GL_LINE_SMOOTH);
    //glEnable(GL_POINT_SMOOTH);
    glEnable(GL_BLEND);
    glHint(GL_LINE_SMOOTH_HINT,GL_DONT_CARE);
    glLineWidth(1.5);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    // Draw everything twice
    // l = 0: shadow
    // l = 1: drawing
    for (int l = 0; l < 2; ++l) {
        if (l == 0) {
            // translate (1,-1) pixels
            glTranslated(pscale.x, -pscale.y, 0);
            glColor3f(0., 0., 0.);
        } else {
            glColor3f(0.8, 0.8, 0.8);
        }
        glBegin(GL_LINE_STRIP);
        glVertex2d(xi1, yi1);
        glVertex2d(xi1, yi2);
        glVertex2d(xi2, yi2);
        glVertex2d(xi2, yi1);
        glVertex2d(xi1, yi1);
        glEnd();

        glBegin(GL_LINE_STRIP);
        glVertex2d(xo1, yo1);
        glVertex2d(xo1, yo2);
        glVertex2d(xo2, yo2);
        glVertex2d(xo2, yo1);
        glVertex2d(xo1, yo1);
        glEnd();

        glPointSize(POINT_SIZE);
        glBegin(GL_POINTS);

        ///draw center
        if (l == 1) {
            if (_ds == eHoveringCenter || _ms == eDraggingCenter) {
                glColor3f(0., 1., 0.);
            } else {
                glColor3f(0.8, 0.8, 0.8);
            }
        }
        glVertex2d(xc, yc);
        //////DRAWING INNER POINTS
        if (_ds == eHoveringInnerBtmLeft || _ms == eDraggingInnerBtmLeft) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xi1, yi1);
        }
        if (_ds == eHoveringInnerBtmMid || _ms == eDraggingInnerBtmMid) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xc, yi1);
        }
        if (_ds == eHoveringInnerBtmRight || _ms == eDraggingInnerBtmRight) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xi2, yi1);
        }
        if (_ds == eHoveringInnerMidLeft || _ms == eDraggingInnerMidLeft) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xi1, yc);
        }
        if (_ds == eHoveringInnerMidRight || _ms == eDraggingInnerMidRight) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xi2, yc);
        }
        if (_ds == eHoveringInnerTopLeft || _ms == eDraggingInnerTopLeft) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xi1, yi2);
        }

        if (_ds == eHoveringInnerTopMid || _ms == eDraggingInnerTopMid) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xc, yi2);
        }

        if (_ds == eHoveringInnerTopRight || _ms == eDraggingInnerTopRight) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xi2, yi2);
        }




        //////DRAWING OUTTER POINTS

        if (_ds == eHoveringOuterBtmLeft || _ms == eDraggingOuterBtmLeft) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xo1, yo1);
        }
        if (_ds == eHoveringOuterBtmMid || _ms == eDraggingOuterBtmMid) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xc, yo1);
        }
        if (_ds == eHoveringOuterBtmRight || _ms == eDraggingOuterBtmRight) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xo2, yo1);
        }
        if (_ds == eHoveringOuterMidLeft || _ms == eDraggingOuterMidLeft) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xo1, yc);
        }
        if (_ds == eHoveringOuterMidRight || _ms == eDraggingOuterMidRight) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xo2, yc);
        }

        if (_ds == eHoveringOuterTopLeft || _ms == eDraggingOuterTopLeft) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xo1, yo2);
        }
        if (_ds == eHoveringOuterTopMid || _ms == eDraggingOuterTopMid) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xc, yo2);
        }
        if (_ds == eHoveringOuterTopRight || _ms == eDraggingOuterTopRight) {
            if (l == 1) {
                glColor3f(0., 1., 0.);
            }
            glVertex2d(xo2, yo2);
        }

        glEnd();

        double handleSizeX = HANDLE_SIZE * pscale.x;
        double handleSizeY = HANDLE_SIZE * pscale.y;

        ///now show small lines at handle positions
        glBegin(GL_LINES);

        if (l == 1) {
            if (_ds == eHoveringInnerMidLeft || _ms == eDraggingInnerMidLeft) {
                glColor3f(0., 1., 0.);
            } else {
                glColor3f(0.8, 0.8, 0.8);
            }
        }
        glVertex2d(xi1, yc);
        glVertex2d(xi1 - handleSizeX, yc);

        if (l == 1) {
            if (_ds == eHoveringInnerTopMid || _ms == eDraggingInnerTopMid) {
                glColor3f(0., 1., 0.);
            } else {
                glColor3f(0.8, 0.8, 0.8);
            }
        }
        glVertex2d(xc, yi2);
        glVertex2d(xc, yi2 + handleSizeY);

        if (l == 1) {
            if (_ds == eHoveringInnerMidRight || _ms == eDraggingInnerMidRight) {
                glColor3f(0., 1., 0.);
            } else {
                glColor3f(0.8, 0.8, 0.8);
            }
        }
        glVertex2d(xi2, yc);
        glVertex2d(xi2 + handleSizeX, yc);

        if (l == 1) {
            if (_ds == eHoveringInnerBtmMid || _ms == eDraggingInnerBtmMid) {
                glColor3f(0., 1., 0.);
            } else {
                glColor3f(0.8, 0.8, 0.8);
            }
        }
        glVertex2d(xc, yi1);
        glVertex2d(xc, yi1 - handleSizeY);

        //////DRAWING OUTTER HANDLES

        if (l == 1) {
            if (_ds == eHoveringOuterMidLeft || _ms == eDraggingOuterMidLeft) {
                glColor3f(0., 1., 0.);
            } else {
                glColor3f(0.8, 0.8, 0.8);
            }
        }
        glVertex2d(xo1, yc);
        glVertex2d(xo1 - handleSizeX, yc);

        if (l == 1) {
            if (_ds == eHoveringOuterTopMid || _ms == eDraggingOuterTopMid) {
                glColor3f(0., 1., 0.);
            } else {
                glColor3f(0.8, 0.8, 0.8);
            }
        }
        glVertex2d(xc, yo2);
        glVertex2d(xc, yo2 + handleSizeY);

        if (l == 1) {
            if (_ds == eHoveringOuterMidRight || _ms == eDraggingOuterMidRight) {
                glColor3f(0., 1., 0.);
            } else {
                glColor3f(0.8, 0.8, 0.8);
            }
        }
        glVertex2d(xo2 + handleSizeX, yc);
        glVertex2d(xo2, yc);

        if (l == 1) {
            if (_ds == eHoveringOuterBtmMid || _ms == eDraggingOuterBtmMid) {
                glColor3f(0., 1., 0.);
            } else {
                glColor3f(0.8, 0.8, 0.8);
            }
        }
        glVertex2d(xc, yo1);
        glVertex2d(xc, yo1 - handleSizeY);
        glEnd();
        
        
        if (l == 1) {
            glColor3f(0.8, 0.8, 0.8);
        }
        std::string name;
        _name->getValue(name);
        TextRenderer::bitmapString(xc, yc, name.c_str());
        
        if (l == 0) {
            // translate (-1,1) pixels
            glTranslated(-pscale.x, pscale.y, 0);
        }
    }

    glPopAttrib();
    
    return true;
}

bool TrackerRegionInteract::penMotion(const OFX::PenArgs &args)
{
    OfxPointD pscale;
    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;

    bool didSomething = false;
    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;

    double xi1, xi2, yi1, yi2, xo1, xo2, yo1, yo2, xc, yc;
    _innerBtmLeft->getValueAtTime( args.time, xi1, yi1);
    _innerTopRight->getValueAtTime(args.time, xi2, yi2);
    _outerBtmLeft->getValueAtTime( args.time, xo1, yo1);
    _outerTopRight->getValueAtTime(args.time, xo2, yo2);
    _center->getValueAtTime(args.time, xc, yc);
    ///innerBtmLeft and outerBtmLeft are relative to the center, make them absolute
    xi1 += xc;
    yi1 += yc;
    xi2 += xc;
    yi2 += yc;
    xo1 += xc;
    yo1 += yc;
    xo2 += xc;
    yo2 += yc;

    bool lastStateWasHovered = _ds != eInactive;

    if (_ms == eIdle) {
        // test center first
        if (       isNearby(args.penPosition, xc,  yc,  POINT_TOLERANCE, pscale)) {
            _ds = eHoveringCenter;
            didSomething = true;
        } else if (isNearby(args.penPosition, xi1, yi1, POINT_TOLERANCE, pscale)) {
            _ds = eHoveringInnerBtmLeft;
            didSomething = true;
        } else if (isNearby(args.penPosition, xi2, yi1, POINT_TOLERANCE, pscale)) {
            _ds = eHoveringInnerBtmRight;
            didSomething = true;
        } else if (isNearby(args.penPosition, xi1, yi2, POINT_TOLERANCE, pscale)) {
            _ds = eHoveringInnerTopLeft;
            didSomething = true;
        } else if (isNearby(args.penPosition, xi2, yi2, POINT_TOLERANCE, pscale)) {
            _ds = eHoveringInnerTopRight;
            didSomething = true;
        } else if (isNearby(args.penPosition, xc,  yi1, POINT_TOLERANCE, pscale)) {
            _ds = eHoveringInnerBtmMid;
            didSomething = true;
        } else if (isNearby(args.penPosition, xi1, yc,  POINT_TOLERANCE, pscale)) {
            _ds = eHoveringInnerMidLeft;
            didSomething = true;
        } else if (isNearby(args.penPosition, xc,  yi2, POINT_TOLERANCE, pscale)) {
            _ds = eHoveringInnerTopMid;
            didSomething = true;
        } else if (isNearby(args.penPosition, xi2, yc,  POINT_TOLERANCE, pscale)) {
            _ds = eHoveringInnerMidRight;
            didSomething = true;
        } else if (isNearby(args.penPosition, xo1, yo1, POINT_TOLERANCE, pscale)) {
            _ds = eHoveringOuterBtmLeft;
            didSomething = true;
        } else if (isNearby(args.penPosition, xo2, yo1, POINT_TOLERANCE, pscale)) {
            _ds = eHoveringOuterBtmRight;
            didSomething = true;
        } else if (isNearby(args.penPosition, xo1, yo2, POINT_TOLERANCE, pscale)) {
            _ds = eHoveringOuterTopLeft;
            didSomething = true;
        } else if (isNearby(args.penPosition, xo2, yo2, POINT_TOLERANCE, pscale)) {
            _ds = eHoveringOuterTopRight;
            didSomething = true;
        } else if (isNearby(args.penPosition, xc,  yo1, POINT_TOLERANCE, pscale)) {
            _ds = eHoveringOuterBtmMid;
            didSomething = true;
        } else if (isNearby(args.penPosition, xo1, yc,  POINT_TOLERANCE, pscale)) {
            _ds = eHoveringOuterMidLeft;
            didSomething = true;
        } else if (isNearby(args.penPosition, xc,  yo2, POINT_TOLERANCE, pscale)) {
            _ds = eHoveringOuterTopMid;
            didSomething = true;
        } else if (isNearby(args.penPosition, xo2, yc,  POINT_TOLERANCE, pscale)) {
            _ds = eHoveringOuterMidRight;
            didSomething = true;
        } else {
            _ds = eInactive;
        }
    }
    
    double multiplier = _controlDown ? 0 : 1;
    if (_ms == eDraggingInnerBtmLeft) {
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
    } else if (_ms == eDraggingInnerBtmRight) {
        _innerTopRightDragPos.y -= delta.y;
        _innerTopRightDragPos.x += delta.x;
        _innerBtmLeftDragPos.y += delta.y;
        _innerBtmLeftDragPos.x -= delta.x;
        
        
        _outerTopRightDragPos.y -= delta.y;
        _outerTopRightDragPos.x += delta.x;
        _outerBtmLeftDragPos.y += delta.y;
        _outerBtmLeftDragPos.x -= delta.x;
        

        didSomething = true;
    } else if (_ms == eDraggingInnerTopMid) {
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
    } else if (_ms == eDraggingInnerBtmMid) {
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
    } else if (_ms == eDraggingOuterBtmLeft) {
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
    } else if (_ms == eDraggingOuterBtmRight) {
        _outerTopRightDragPos.y -= multiplier * delta.y;
        _outerTopRightDragPos.x +=  delta.x;
        _outerBtmLeftDragPos.y += delta.y;
        if (!_controlDown) {
            _outerBtmLeftDragPos.x -= delta.x;
        }
        didSomething = true;
    } else if (_ms == eDraggingOuterTopMid) {
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
    } else if (_ms == eDraggingOuterBtmMid) {
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
        /// outer rect must at least contain the inner rect

        if (_outerBtmLeftDragPos.x > xi1) {
            _outerBtmLeftDragPos.x = xi1;
        }

        if (_outerBtmLeftDragPos.y > yi1) {
            _outerBtmLeftDragPos.y = yi1;
        }
        
        if (_outerTopRightDragPos.x < xi2) {
            _outerTopRightDragPos.x = xi2;
        }
        if (_outerTopRightDragPos.y < yi2) {
            _outerTopRightDragPos.y = yi2;
        }
    }
    
    if (isDraggingInnerPoint()) {
        /// inner rect must contain center point
        if (_innerBtmLeftDragPos.x > xc) {
            double diffX = _innerBtmLeftDragPos.x - xc;
            _innerBtmLeftDragPos.x = xc;
            _outerBtmLeftDragPos.x -= diffX;
            _outerTopRightDragPos.x += multiplier * diffX;
            _innerTopRightDragPos.x += multiplier * diffX;
        }
        if (_innerBtmLeftDragPos.y > yc) {
            double diffY = _innerBtmLeftDragPos.y - yc;
            _innerBtmLeftDragPos.y = yc;
            _outerBtmLeftDragPos.y -= diffY;
            _outerTopRightDragPos.y += multiplier * diffY;
            _innerTopRightDragPos.y += multiplier * diffY;
        }
        if (_innerTopRightDragPos.x <= xc) {
            double diffX = _innerTopRightDragPos.x - xc;
            _innerTopRightDragPos.x = xc;
            _outerTopRightDragPos.x += diffX;
            _outerBtmLeftDragPos.x -= multiplier * diffX;
            _innerBtmLeftDragPos.x -= multiplier * diffX;
        }
        if (_innerTopRightDragPos.y <= yc) {
            double diffY = _innerTopRightDragPos.y - yc;
            _innerTopRightDragPos.y = yc;
            _outerTopRightDragPos.y -= diffY;
            _outerBtmLeftDragPos.y -= multiplier * diffY;
            _innerBtmLeftDragPos.y -= multiplier * diffY;
        }
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
    OfxPointD pscale;
    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;

    bool didSomething = false;
    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;

    double xi1, xi2, yi1, yi2, xo1, xo2, yo1, yo2, xc, yc;
    _innerBtmLeft->getValueAtTime( args.time, xi1, yi1);
    _innerTopRight->getValueAtTime(args.time, xi2, yi2);
    _outerBtmLeft->getValueAtTime( args.time, xo1, yo1);
    _outerTopRight->getValueAtTime(args.time, xo2, yo2);
    _center->getValueAtTime(args.time, xc, yc);
    ///innerBtmLeft and outerBtmLeft are relative to the center, make them absolute
    xi1 += xc;
    yi1 += yc;
    xi2 += xc;
    yi2 += yc;
    xo1 += xc;
    yo1 += yc;
    xo2 += xc;
    yo2 += yc;

    // test center first
    if (       isNearby(args.penPosition, xc,  yc,  POINT_TOLERANCE, pscale)) {
        _ms = eDraggingCenter;
        didSomething = true;
    } else if (isNearby(args.penPosition, xi1, yi1, POINT_TOLERANCE, pscale)) {
        _ms = eDraggingInnerBtmLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, xi2, yi1, POINT_TOLERANCE, pscale)) {
        _ms = eDraggingInnerBtmRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, xi1, yi2, POINT_TOLERANCE, pscale)) {
        _ms = eDraggingInnerTopLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, xi2, yi2, POINT_TOLERANCE, pscale)) {
        _ms = eDraggingInnerTopRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, xc,  yi1, POINT_TOLERANCE, pscale)) {
        _ms = eDraggingInnerBtmMid;
        didSomething = true;
    } else if (isNearby(args.penPosition, xi1, yc,  POINT_TOLERANCE, pscale)) {
        _ms = eDraggingInnerMidLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, xc,  yi2, POINT_TOLERANCE, pscale)) {
        _ms = eDraggingInnerTopMid;
        didSomething = true;
    } else if (isNearby(args.penPosition, xi2, yc,  POINT_TOLERANCE, pscale)) {
        _ms = eDraggingInnerMidRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, xo1, yo1, POINT_TOLERANCE, pscale)) {
        _ms = eDraggingOuterBtmLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, xo2, yo1, POINT_TOLERANCE, pscale)) {
        _ms = eDraggingOuterBtmRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, xo1, yo2, POINT_TOLERANCE, pscale)) {
        _ms = eDraggingOuterTopLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, xo2, yo2, POINT_TOLERANCE, pscale)) {
        _ms = eDraggingOuterTopRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, xc,  yo1, POINT_TOLERANCE, pscale)) {
        _ms = eDraggingOuterBtmMid;
        didSomething = true;
    } else if (isNearby(args.penPosition, xo1, yc,  POINT_TOLERANCE, pscale)) {
        _ms = eDraggingOuterMidLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, xc,  yo2, POINT_TOLERANCE, pscale)) {
        _ms = eDraggingOuterTopMid;
        didSomething = true;
    } else if (isNearby(args.penPosition, xo2, yc,  POINT_TOLERANCE, pscale)) {
        _ms = eDraggingOuterMidRight;
        didSomething = true;
    } else {
        _ms = eIdle;
    }
 
    
    ///Keep the points in absolute coordinates
    _innerBtmLeftDragPos.x  = xi1;
    _innerBtmLeftDragPos.y  = yi1;
    _innerTopRightDragPos.x = xi2;
    _innerTopRightDragPos.y = yi2;
    _outerBtmLeftDragPos.x  = xo1;
    _outerBtmLeftDragPos.y  = yo1;
    _outerTopRightDragPos.x = xo2;
    _outerTopRightDragPos.y = yo2;
    _centerDragPos.x        = xc;
    _centerDragPos.y        = yc;

    _lastMousePos = args.penPosition;
    return didSomething;
}

bool TrackerRegionInteract::isDraggingInnerPoint() const
{
    return _ms == eDraggingInnerTopLeft ||
            _ms == eDraggingInnerTopRight ||
            _ms == eDraggingInnerBtmLeft ||
            _ms == eDraggingInnerBtmRight ||
            _ms == eDraggingInnerTopMid ||
            _ms == eDraggingInnerMidRight ||
            _ms == eDraggingInnerBtmMid ||
            _ms == eDraggingInnerMidLeft;
}

bool TrackerRegionInteract::isDraggingOuterPoint() const
{
    return _ms == eDraggingOuterTopLeft ||
    _ms == eDraggingOuterTopRight ||
    _ms == eDraggingOuterBtmLeft ||
    _ms == eDraggingOuterBtmRight ||
    _ms == eDraggingOuterTopMid ||
    _ms == eDraggingOuterMidRight ||
    _ms == eDraggingOuterBtmMid ||
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
        return !_altDown;
    } else if (args.keySymbol == kOfxKey_Alt_L || args.keySymbol == kOfxKey_Alt_R) {
        _altDown = true;
        return false;
    }
    return false;
}

bool TrackerRegionInteract::keyUp(const OFX::KeyArgs &args)
{
    if (args.keySymbol == kOfxKey_Control_L || args.keySymbol == kOfxKey_Control_R) {
        _controlDown = false;
        return !_altDown;
    } else if (args.keySymbol == kOfxKey_Alt_L || args.keySymbol == kOfxKey_Alt_R) {
        _altDown = false;
        return false;
    }
    return false;
}