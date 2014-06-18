/*
 OFX generic rectangle interact with 4 corner points + center point and 4 mid-points.
 You can use it to define any rectangle in an image resizable by the user.
  
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
#ifndef __Misc__ofxsRectangleInteract__
#define __Misc__ofxsRectangleInteract__

#include <ofxsInteract.h>
#include <ofxsImageEffect.h>

#define kRectInteractBtmLeftParamName "BottomLeft"
#define kRectInteractBtmLeftParamLabel "Bottom Left"
#define kRectInteractSizeParamName "Size"
#define kRectInteractSizeParamLabel "Size"
/**
 * @brief In order to work the plug-in using this interact must have 2 parameters named after
 * the defines above.
 *
 **/
class RectangleInteract : public OFX::OverlayInteract
{
    
    enum MouseState
    {
        eIdle = 0,
        eDraggingTopLeft,
        eDraggingTopRight,
        eDraggingBottomLeft,
        eDraggingBottomRight,
        eDraggingCenter,
        eDraggingMidTop,
        eDraggingMidRight,
        eDraggingMidBtm,
        eDraggingMidLeft
    };
    
    enum DrawState
    {
        eInactive = 0,
        eHoveringTopLeft,
        eHoveringTopRight,
        eHoveringBottomLeft,
        eHoveringBottomRight,
        eHoveringCenter,
        eHoveringMidTop,
        eHoveringMidRight,
        eHoveringMidBtm,
        eHoveringMidLeft
    };
    
public:
    
    RectangleInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _lastMousePos()
    , _ms(eIdle)
    , _ds(eInactive)
    , _btmLeft(0)
    , _size(0)
    {
        _btmLeft = effect->fetchDouble2DParam(kRectInteractBtmLeftParamName);
        _size = effect->fetchDouble2DParam(kRectInteractSizeParamName);
        addParamToSlaveTo(_btmLeft);
        addParamToSlaveTo(_size);
        assert(_btmLeft && _size);
    }
    
    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args);
    virtual bool penMotion(const OFX::PenArgs &args);
    virtual bool penDown(const OFX::PenArgs &args);
    virtual bool penUp(const OFX::PenArgs &args);
    virtual bool keyDown(const OFX::KeyArgs &/*args*/) { return false; }
    virtual bool keyUp(const OFX::KeyArgs &/*args*/) { return false; }
    
protected:
    
    
    /**
     * @brief This method returns the bottom left point. The base implementation just returns the value
     * of the _btmLeft parameter at the given time.
     * One could override this function to  do more complex stuff based on other parameters state like the Crop plug-in does.
     **/
    virtual OfxPointD getBottomLeft(OfxTime time) const;
    
    /**
     * @brief This is called right before any call to allowXXX is made.
     * This way you can query values of a parameter and store it away without having to do this
     * at every allowXXX call.
     **/
    virtual void aboutToCheckInteractivity(OfxTime /*time*/) {}
    
    /**
     * @brif These can be overriden to disallow interaction with a point.
     **/
    virtual bool allowTopLeftInteraction() const { return true; }
    virtual bool allowTopRightInteraction() const { return true; }
    virtual bool allowBottomRightInteraction() const { return true; }
    virtual bool allowBottomLeftInteraction() const { return true; }
    virtual bool allowMidTopInteraction() const { return true; }
    virtual bool allowMidRightInteraction() const { return true; }
    virtual bool allowMidBottomInteraction() const { return true; }
    virtual bool allowMidLeftInteraction() const { return true; }
    virtual bool allowCenterInteraction() const { return true; }

private:
    
    
    
    bool isNearbyTopLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyTopRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyBtmLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyBtmRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyMidTop(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyMidRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyMidLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyMidBtm(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyCenter(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    
    OfxPointD _lastMousePos;
    MouseState _ms;
    DrawState _ds;
    OFX::Double2DParam* _btmLeft;
    OFX::Double2DParam* _size;
    OfxPointD _btmLeftDragPos;
    OfxPointD _sizeDrag;
    
};

class RectangleOverlayDescriptor : public OFX::DefaultEffectOverlayDescriptor<RectangleOverlayDescriptor, RectangleInteract> {};


#endif /* defined(__Misc__ofxsRectangleInteract__) */
