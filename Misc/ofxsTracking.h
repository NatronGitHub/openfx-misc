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

#define kTrackPatternBoxBtmLeftParamName "patternBoxBtmLeft"
#define kTrackPatternBoxBtmLeftParamLabel "Pattern bottom left"
#define kTrackPatternBoxBtmLeftParamHint "The bottom left corner of the inner pattern box. The coordinates are relative to the center point."

#define kTrackPatternBoxTopRightParamName "patternBoxTopRight"
#define kTrackPatternBoxTopRightParamLabel "Pattern top right"
#define kTrackPatternBoxTopRightParamHint "The top right corner of the inner pattern box. The coordinates are relative to the center point."

#define kTrackSearchBoxBtmLeftParamName "searchBoxBtmLeft"
#define kTrackSearchBoxBtmLeftParamLabel "Search area bottom left"
#define kTrackSearchBoxBtmLeftParamHint "The bottom left corner of the search area. The coordinates are relative to the center point."

#define kTrackSearchBoxTopRightParamName "searchBoxTopRight"
#define kTrackSearchBoxTopRightParamLabel "Search area top right"
#define kTrackSearchBoxTopRightParamHint "The top right corner of the search area. The coordinates are relative to the center point."

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
        OfxTime first; //<! the first frame to track *from*
        OfxTime last; //<! the last frame to track *from* (can be the same as first)
        bool forward; //<! tracking direction
        InstanceChangeReason reason;
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
    OFX::Double2DParam* _innerTopRight;
    OFX::Double2DParam* _outerBtmLeft;
    OFX::Double2DParam* _outerTopRight;
    
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
 * - An outer rectangle which defines the region where we should look for the pattern in the previous/following frames.
 *
 * The inner and outer rectangle are defined respectively by their bottom left corner and their size (width/height).
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
        eDraggingInnerBtmLeft,
        eDraggingInnerBtmRight,
        eDraggingInnerTopMid,
        eDraggingInnerMidRight,
        eDraggingInnerBtmMid,
        eDraggingInnerMidLeft,
        
        eDraggingOuterTopLeft,
        eDraggingOuterTopRight,
        eDraggingOuterBtmLeft,
        eDraggingOuterBtmRight,
        eDraggingOuterTopMid,
        eDraggingOuterMidRight,
        eDraggingOuterBtmMid,
        eDraggingOuterMidLeft
    };
    
    enum DrawState
    {
        eInactive = 0,
        eHoveringCenter,

        eHoveringInnerTopLeft,
        eHoveringInnerTopRight,
        eHoveringInnerBtmLeft,
        eHoveringInnerBtmRight,
        eHoveringInnerTopMid,
        eHoveringInnerMidRight,
        eHoveringInnerBtmMid,
        eHoveringInnerMidLeft,
        
        eHoveringOuterTopLeft,
        eHoveringOuterTopRight,
        eHoveringOuterBtmLeft,
        eHoveringOuterBtmRight,
        eHoveringOuterTopMid,
        eHoveringOuterMidRight,
        eHoveringOuterBtmMid,
        eHoveringOuterMidLeft
    };
    
public:
    
    TrackerRegionInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _lastMousePos()
    , _ms(eIdle)
    , _ds(eInactive)
    , _center(0)
    , _innerBtmLeft(0)
    , _innerTopRight(0)
    , _outerBtmLeft(0)
    , _outerTopRight(0)
    , _name(0)
    , _centerDragPos()
    , _innerBtmLeftDragPos()
    , _innerTopRightDragPos()
    , _outerBtmLeftDragPos()
    , _outerTopRightDragPos()
    , _controlDown(false)
    , _altDown(false)
    {
        _center = effect->fetchDouble2DParam(kTrackCenterPointParamName);
        _innerBtmLeft = effect->fetchDouble2DParam(kTrackPatternBoxBtmLeftParamName);
        _innerTopRight = effect->fetchDouble2DParam(kTrackPatternBoxTopRightParamName);
        _outerBtmLeft = effect->fetchDouble2DParam(kTrackSearchBoxBtmLeftParamName);
        _outerTopRight = effect->fetchDouble2DParam(kTrackSearchBoxTopRightParamName);
        _name = effect->fetchStringParam(kOfxParamStringSublabelName);
        addParamToSlaveTo(_center);
        addParamToSlaveTo(_innerBtmLeft);
        addParamToSlaveTo(_innerTopRight);
        addParamToSlaveTo(_outerBtmLeft);
        addParamToSlaveTo(_outerTopRight);
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
    bool isDraggingInnerPoint() const;
    bool isDraggingOuterPoint() const;
    
    OfxPointD _lastMousePos;
    MouseState _ms;
    DrawState _ds;
    
    OFX::Double2DParam* _center;
    
    OFX::Double2DParam* _innerBtmLeft;
    OFX::Double2DParam* _innerTopRight;
    OFX::Double2DParam* _outerBtmLeft;
    OFX::Double2DParam* _outerTopRight;
    OFX::StringParam* _name;
    
    OfxPointD _centerDragPos;
    
    ///Here the btm left points are NOT relative to the center
    OfxPointD _innerBtmLeftDragPos;
    OfxPointD _innerTopRightDragPos;
    OfxPointD _outerBtmLeftDragPos;
    OfxPointD _outerTopRightDragPos;
    
    bool _controlDown;
    bool _altDown;
    
};

class TrackerRegionOverlayDescriptor : public OFX::DefaultEffectOverlayDescriptor<TrackerRegionOverlayDescriptor, TrackerRegionInteract> {};


#endif /* defined(__Misc__ofxsTracking__) */
