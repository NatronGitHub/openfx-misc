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
#ifndef __Misc__ofxsTracking__
#define __Misc__ofxsTracking__

#include "ofxsImageEffect.h"
#include "ofxNatron.h"

#define kTrackCenterPointParamName "center"
#define kTrackCenterPointParamLabel "Center"
#define kTrackCenterPointParamHint "The center point to track"

#define kTrackPatternBoxPositionParamName "patternBoxPosition"
#define kTrackPatternBoxPositionParamLabel "Pattern box position"
#define kTrackPatternBoxPositionParamHint "The bottom left corner of the inner pattern box. The coordinates are relative to the center point."

#define kTrackPatternBoxSizeParamName "patternBoxSize"
#define kTrackPatternBoxSizeParamLabel "patternBoxLabel"
#define kTrackPatternBoxSizeParamHint "Width and height of the pattern box."

#define kTrackSearchBoxPositionParamName "searchBoxPosition"
#define kTrackSearchBoxPositionParamLabel "Search box position"
#define kTrackSearchBoxPositionParamHint "The bottom left corner of the search area. The coordinates are relative to the center point."

#define kTrackSearchBoxSizeParamName "searchBoxSize"
#define kTrackSearchBoxSizeParamLabel "Search box size"
#define kTrackSearchBoxSizeParamHint "Width and height of the search area."

#define kTrackPreviousParamName "trackPrevious"
#define kTrackPreviousParamLabel "Track previous"
#define kTrackPreviousParamHint "Track pattern to previous frame"

#define kTrackNextParamName "trackNext"
#define kTrackNextParamLabel "Track next"
#define kTrackNextParamHint "Track pattern to next frame"

#define kTrackBackwardParamName "trackBackward"
#define kTrackBackwardParamLabel "Track backward"
#define kTrackBackwardParamHint "Track pattern to the beginning of the sequence"

#define kTrackForwardParamName "trackForward"
#define kTrackForwardParamLabel "Track forward"
#define kTrackForwardParamHint "Track pattern to the end of the sequence"

#define kTrackLabelParamName kOfxParamStringSublabelName // defined in ofxNatron.h
#define kTrackLabelParamLabel "Track name"
#define kTrackLabelParamHint "The name of the track, as it appears in the user interface."
#define kTrackLabelParamDefault "Track"

namespace OFX
{
    struct TrackArguments {
        ///first is not necesserarily lesser than last.
        OfxTime first;
        OfxTime last;
        bool forward;
    };
}

class GenericTrackerPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    GenericTrackerPlugin(OfxImageEffectHandle handle);
    
    /** 
     * @brief Nothing to do since we're identity. The host should always render the image of the input.
     **/
    virtual void render(const OFX::RenderArguments &/*args*/) /*OVERRIDE FINAL*/ {}
    
    /**
     * @brief Returns true always at the same time and for the source clip.
     **/
    virtual bool isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &identityTime) /*OVERRIDE FINAL*/;
    
    /**
     * @brief Handles the push buttons actions.
     **/
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);
    

protected:
    
    /**
     * @brief Override to track the entire range between [first,last]. 
     * @param forward If true then it should track from first to last, otherwise it should track
     * from last to first.
     * @param currentTime The current time at which the track has been requested.
     **/
    virtual void trackRange(const OFX::TrackArguments& args) = 0;
    
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    
    OFX::Double2DParam* _center;
    OFX::Double2DParam* _innerBtmLeft;
    OFX::Double2DParam* _innerSize;
    OFX::Double2DParam* _outterBtmLeft;
    OFX::Double2DParam* _outterSize;
    
    OFX::PushButtonParam* _backwardButton;
    OFX::PushButtonParam* _prevButton;
    OFX::PushButtonParam* _nextButton;
    OFX::PushButtonParam* _forwardButton;
    
    OFX::StringParam* _instanceName;
    
private:
    
    
    
};

void genericTrackerDescribe(OFX::ImageEffectDescriptor &desc);

OFX::PageParamDescriptor* genericTrackerDescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context);

void genericTrackerDescribePointParameters(OFX::ImageEffectDescriptor &desc,OFX::PageParamDescriptor* page);

/**
 * @brief This class represents the interact associated with one track.
 * It is composed of the following elements:
 * - A point which is the center point of the pattern to track
 * - An inner rectangle which defines the bounding box of the pattern to track
 * - An outter rectangle which defines the region where we should look for the pattern in the previous/following frames.
 *
 * The inner and outter rectangle are defined respectively by their bottom left corner and their size (width/height).
 * The bottom left corner of these rectangles defines an offset relative to the center point instead of absolute coordinates.
 * It makes it really easier everywhere in the tracker to manipulate coordinates.
 **/
class TrackerRegionInteract : public OFX::OverlayInteract
{
    
    enum MouseState
    {
        eIdle = 0,
        eDraggingCenter,

        eDraggingInnerTopLeft,
        eDraggingInnerTopRight,
        eDraggingInnerBottomLeft,
        eDraggingInnerBottomRight,
        eDraggingInnerMidTop,
        eDraggingInnerMidRight,
        eDraggingInnerMidBtm,
        eDraggingInnerMidLeft,
        
        eDraggingOutterTopLeft,
        eDraggingOutterTopRight,
        eDraggingOutterBottomLeft,
        eDraggingOutterBottomRight,
        eDraggingOutterMidTop,
        eDraggingOutterMidRight,
        eDraggingOutterMidBtm,
        eDraggingOutterMidLeft
    };
    
    enum DrawState
    {
        eInactive = 0,
        eHoveringCenter,

        eHoveringInnerTopLeft,
        eHoveringInnerTopRight,
        eHoveringInnerBottomLeft,
        eHoveringInnerBottomRight,
        eHoveringInnerMidTop,
        eHoveringInnerMidRight,
        eHoveringInnerMidBtm,
        eHoveringInnerMidLeft,
        
        eHoveringOutterTopLeft,
        eHoveringOutterTopRight,
        eHoveringOutterBottomLeft,
        eHoveringOutterBottomRight,
        eHoveringOutterMidTop,
        eHoveringOutterMidRight,
        eHoveringOutterMidBtm,
        eHoveringOutterMidLeft
    };
    
public:
    
    TrackerRegionInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _lastMousePos()
    , _ms(eIdle)
    , _ds(eInactive)
    , _center(0)
    , _innerBtmLeft(0)
    , _innerSize(0)
    , _outterBtmLeft(0)
    , _outterSize(0)
    , _name(0)
    , _centerDragPos()
    , _innerBtmLeftDragPos()
    , _innerSizeDrag()
    , _outterBtmLeftDragPos()
    , _outterSizeDrag()
    , _controlDown(false)
    {
        _center = effect->fetchDouble2DParam(kTrackCenterPointParamName);
        _innerBtmLeft = effect->fetchDouble2DParam(kTrackPatternBoxPositionParamName);
        _innerSize = effect->fetchDouble2DParam(kTrackPatternBoxSizeParamName);
        _outterBtmLeft = effect->fetchDouble2DParam(kTrackSearchBoxPositionParamName);
        _outterSize = effect->fetchDouble2DParam(kTrackSearchBoxSizeParamName);
        _name = effect->fetchStringParam(kOfxParamStringSublabelName);
        addParamToSlaveTo(_center);
        addParamToSlaveTo(_innerBtmLeft);
        addParamToSlaveTo(_innerSize);
        addParamToSlaveTo(_outterBtmLeft);
        addParamToSlaveTo(_outterSize);
        addParamToSlaveTo(_name);
    }
    
    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args);
    virtual bool penMotion(const OFX::PenArgs &args);
    virtual bool penDown(const OFX::PenArgs &args);
    virtual bool penUp(const OFX::PenArgs &args);
    virtual bool keyDown(const OFX::KeyArgs &args);
    virtual bool keyUp(const OFX::KeyArgs &args);
    
    
private:
    
    
    ///All the functions below assume that the size parameter is the width/height of either the inner or outter rectangle
    ///and btmLeft is the absolute coordinates of either the inner or outter rectangle.
    bool isNearbyTopLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyTopRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyBtmLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyBtmRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyMidTop(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyMidRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyMidLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyMidBtm(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;

    
    bool isNearbyCenter(const OfxPointD& pos,double tolerance,const OfxPointD& center) const;
    
    bool isDraggingInnerPoint() const;
    bool isDraggingOutterPoint() const;
    
    OfxPointD _lastMousePos;
    MouseState _ms;
    DrawState _ds;
    
    OFX::Double2DParam* _center;
    
    OFX::Double2DParam* _innerBtmLeft;
    OFX::Double2DParam* _innerSize;
    OFX::Double2DParam* _outterBtmLeft;
    OFX::Double2DParam* _outterSize;
    OFX::StringParam* _name;
    
    OfxPointD _centerDragPos;
    
    ///Here the btm left points are NOT relative to the center
    OfxPointD _innerBtmLeftDragPos;
    OfxPointD _innerSizeDrag;
    OfxPointD _outterBtmLeftDragPos;
    OfxPointD _outterSizeDrag;
    
    bool _controlDown;
    
};

class TrackerRegionOverlayDescriptor : public OFX::DefaultEffectOverlayDescriptor<TrackerRegionOverlayDescriptor, TrackerRegionInteract> {};


#endif /* defined(__Misc__ofxsTracking__) */
