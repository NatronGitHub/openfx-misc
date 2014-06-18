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

#include "ofxsRectangleInteract.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

bool RectangleInteract::isNearbyTopLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
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

bool RectangleInteract::isNearbyTopRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
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

bool RectangleInteract::isNearbyBtmLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    if (pos.x >= (btmLeft.x - tolerance) && pos.x <= (btmLeft.x + tolerance) &&
        pos.y >= (btmLeft.y - tolerance) && pos.y <= (btmLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }
    
}

bool RectangleInteract::isNearbyBtmRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
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

bool RectangleInteract::isNearbyMidTop(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
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

bool RectangleInteract::isNearbyMidRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
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

bool RectangleInteract::isNearbyMidLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
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

bool RectangleInteract::isNearbyMidBtm(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
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
bool RectangleInteract::isNearbyCenter(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD center;
    center.x = btmLeft.x + size.x / 2.;
    center.y = btmLeft.y + size.y / 2.;
    if (pos.x >= (center.x - tolerance) && pos.x <= (center.x + tolerance) &&
        pos.y >= (center.y - tolerance) && pos.y <= (center.y + tolerance)) {
        return true;
    } else {
        return false;
    }
    
}


bool RectangleInteract::draw(const OFX::DrawArgs &args)
{
    OfxPointD btmLeft,size;
    if (_ms != eIdle) {
        btmLeft = _btmLeftDragPos;
        size = _sizeDrag;
    } else {
        btmLeft = getBottomLeft(args.time);
        _size->getValueAtTime(args.time, size.x, size.y);
    }
    
    OfxPointD topLeft,midTop,topRight,midRight,btmRight,midBtm,midLeft,center;
    topLeft.x = btmLeft.x;
    topLeft.y = btmLeft.y + size.y;
    midTop.x = btmLeft.x + size.x / 2.;
    midTop.y = topLeft.y;
    topRight.x = btmLeft.x + size.x;
    topRight.y = topLeft.y;
    midRight.x = topRight.x;
    midRight.y = btmLeft.y + size.y / 2.;
    btmRight.x = midRight.x;
    btmRight.y = btmLeft.y;
    midBtm.x = midTop.x;
    midBtm.y = btmLeft.y;
    midLeft.x = btmLeft.x;
    midLeft.y = midRight.y;
    center.x = midTop.x;
    center.y = midRight.y;
    
    glColor4f(0.9, 0.9, 0.9, 1.);
    glBegin(GL_LINE_STRIP);
    glVertex2d(btmLeft.x, btmLeft.y);
    glVertex2d(topLeft.x, topLeft.y);
    glVertex2d(topRight.x, topRight.y);
    glVertex2d(btmRight.x, btmRight.y);
    glVertex2d(btmLeft.x, btmLeft.y);
    glEnd();
    
    glPointSize(6);
    glBegin(GL_POINTS);
    if (_ds == eHoveringBottomLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    if (allowBottomLeftInteraction()) {
        glVertex2d(btmLeft.x, btmLeft.y);
    }
    if (_ds == eHoveringMidLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    if (allowMidLeftInteraction()) {
        glVertex2d(midLeft.x, midLeft.y);
    }
    if (_ds == eHoveringTopLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    if (allowTopLeftInteraction()) {
        glVertex2d(topLeft.x, topLeft.y);
    }
    if (_ds == eHoveringMidTop) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    if (allowMidTopInteraction()) {
        glVertex2d(midTop.x, midTop.y);
    }
    if (_ds == eHoveringTopRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    if (allowTopRightInteraction()) {
        glVertex2d(topRight.x, topRight.y);
    }
    if (_ds == eHoveringMidRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    if (allowMidRightInteraction()) {
        glVertex2d(midRight.x, midRight.y);
    }
    if (_ds == eHoveringBottomRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    if (allowBottomRightInteraction()) {
        glVertex2d(btmRight.x, btmRight.y);
    }
    if (_ds == eHoveringMidBtm) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    if (allowMidBottomInteraction()) {
        glVertex2d(midBtm.x, midBtm.y);
    }
    glEnd();
    glPointSize(1);
    
    ///draw center cross hair
    double lineSizeX = 7 * args.pixelScale.x;
    double lineSizeY = 7 * args.pixelScale.y;
    glBegin(GL_LINES);
    if (_ds == eHoveringCenter) {
        glColor4f(0., 1., 0., 1.);
    } else if (!allowCenterInteraction()) {
        glColor4f(0.5, 0.5, 0.5, 1);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(center.x - lineSizeX, center.y);
    glVertex2d(center.x + lineSizeX, center.y);
    glVertex2d(center.x, center.y - lineSizeY);
    glVertex2d(center.x, center.y + lineSizeY);
    glEnd();
    
    
    return true;
}

bool RectangleInteract::penMotion(const OFX::PenArgs &args)
{
    bool didSomething = false;
    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;
    
    double selectionTol = 15. * args.pixelScale.x;
    
    OfxPointD size;
    _size->getValueAtTime(args.time, size.x, size.y);
    
    OfxPointD btmLeft = getBottomLeft(args.time);
    bool lastStateWasHovered = _ds != eInactive;
    
    
    aboutToCheckInteractivity(args.time);
    if (isNearbyBtmLeft(args.penPosition, selectionTol, size, btmLeft) && allowBottomLeftInteraction()) {
        _ds = eHoveringBottomLeft;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, selectionTol, size, btmLeft)  && allowBottomRightInteraction()) {
        _ds = eHoveringBottomRight;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, selectionTol, size, btmLeft) && allowTopRightInteraction()) {
        _ds = eHoveringTopRight;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, selectionTol, size, btmLeft)  && allowTopLeftInteraction()) {
        _ds = eHoveringTopLeft;
        didSomething = true;
    } else if (isNearbyMidTop(args.penPosition, selectionTol, size, btmLeft) && allowMidTopInteraction()) {
        _ds = eHoveringMidTop;
        didSomething = true;
    } else if (isNearbyMidRight(args.penPosition, selectionTol, size, btmLeft) && allowMidRightInteraction()) {
        _ds = eHoveringMidRight;
        didSomething = true;
    } else if (isNearbyMidBtm(args.penPosition, selectionTol, size, btmLeft)  && allowMidBottomInteraction()) {
        _ds = eHoveringMidBtm;
        didSomething = true;
    } else if (isNearbyMidLeft(args.penPosition, selectionTol, size, btmLeft)  && allowMidLeftInteraction()) {
        _ds = eHoveringMidLeft;
        didSomething = true;
    } else if (isNearbyCenter(args.penPosition, selectionTol, size, btmLeft)  && allowCenterInteraction()) {
        _ds = eHoveringCenter;
        didSomething = true;
    } else {
        _ds = eInactive;
    }
    
    if (_ms == eDraggingBottomLeft) {
        OfxPointD topRight;
        topRight.x = _btmLeftDragPos.x + _sizeDrag.x;
        topRight.y = _btmLeftDragPos.y + _sizeDrag.y;
        _btmLeftDragPos.x += delta.x;
        _btmLeftDragPos.y += delta.y;
        _sizeDrag.x = topRight.x - _btmLeftDragPos.x;
        _sizeDrag.y = topRight.y - _btmLeftDragPos.y;
        didSomething = true;
    } else if (_ms == eDraggingTopLeft) {
        OfxPointD btmRight;
        btmRight.x = _btmLeftDragPos.x + _sizeDrag.x;
        btmRight.y = _btmLeftDragPos.y;
        _btmLeftDragPos.x += delta.x;
        _sizeDrag.y += delta.y;
        _sizeDrag.x = btmRight.x - _btmLeftDragPos.x;
        didSomething = true;
    } else if (_ms == eDraggingTopRight) {
        _sizeDrag.x += delta.x;
        _sizeDrag.y += delta.y;
        didSomething = true;
    } else if (_ms == eDraggingBottomRight) {
        OfxPointD topLeft;
        topLeft.x = _btmLeftDragPos.x;
        topLeft.y = _btmLeftDragPos.y + _sizeDrag.y;
        _sizeDrag.x += delta.x;
        _btmLeftDragPos.y += delta.y;
        _sizeDrag.y = topLeft.y - _btmLeftDragPos.y;
        didSomething = true;
    } else if (_ms == eDraggingMidTop) {
        _sizeDrag.y += delta.y;
        didSomething = true;
    } else if (_ms == eDraggingMidRight) {
        _sizeDrag.x += delta.x;
        didSomething = true;
    } else if (_ms == eDraggingMidBtm) {
        double top = _btmLeftDragPos.y + _sizeDrag.y;
        _btmLeftDragPos.y += delta.y;
        _sizeDrag.y = top - _btmLeftDragPos.y;
        didSomething = true;
    } else if (_ms == eDraggingMidLeft) {
        double right = _btmLeftDragPos.x + _sizeDrag.x;
        _btmLeftDragPos.x += delta.x;
        _sizeDrag.x = right - _btmLeftDragPos.x;
        didSomething = true;
    } else if (_ms == eDraggingCenter) {
        _btmLeftDragPos.x += delta.x;
        _btmLeftDragPos.y += delta.y;
        didSomething = true;
    }
    
    
    //if size is negative shift bottom left
    if (_sizeDrag.x < 0) {
        if (_ms == eDraggingBottomLeft) {
            _ms = eDraggingBottomRight;
        } else if (_ms == eDraggingMidLeft) {
            _ms = eDraggingMidRight;
        } else if (_ms == eDraggingTopLeft) {
            _ms = eDraggingTopRight;
        } else if (_ms == eDraggingBottomRight) {
            _ms = eDraggingBottomLeft;
        } else if (_ms == eDraggingMidRight) {
            _ms = eDraggingMidLeft;
        } else if (_ms == eDraggingTopRight) {
            _ms = eDraggingTopLeft;
        }
        
        _btmLeftDragPos.x += _sizeDrag.x;
        _sizeDrag.x = - _sizeDrag.x;
    }
    if (_sizeDrag.y < 0) {
        if (_ms == eDraggingTopLeft) {
            _ms = eDraggingBottomLeft;
        } else if (_ms == eDraggingMidTop) {
            _ms = eDraggingMidBtm;
        } else if (_ms == eDraggingTopRight) {
            _ms = eDraggingBottomRight;
        } else if (_ms == eDraggingBottomLeft) {
            _ms = eDraggingTopLeft;
        } else if (_ms == eDraggingMidBtm) {
            _ms = eDraggingMidTop;
        } else if (_ms == eDraggingBottomRight) {
            _ms = eDraggingTopRight;
        }
        
        _btmLeftDragPos.y += _sizeDrag.y;
        _sizeDrag.y = - _sizeDrag.y;
    }
    
    ///forbid 0 pixels wide crop rectangles
    if (_sizeDrag.x < 1) {
        _sizeDrag.x = 1;
    }
    if (_sizeDrag.y < 1) {
        _sizeDrag.y = 1;
    }
    
    ///repaint if we toggled off a hovered handle
    if (lastStateWasHovered && !didSomething) {
        didSomething = true;
    }
    
    _lastMousePos = args.penPosition;
    return didSomething;
}

bool RectangleInteract::penDown(const OFX::PenArgs &args)
{
    bool didSomething = false;
    
    double selectionTol = 15. * args.pixelScale.x;
    
    OfxPointD size;
    _size->getValueAtTime(args.time, size.x, size.y);
    
    
    OfxPointD btmLeft = getBottomLeft(args.time);
    
    aboutToCheckInteractivity(args.time);
    
    if (isNearbyBtmLeft(args.penPosition, selectionTol, size, btmLeft)  && allowBottomLeftInteraction()) {
        _ms = eDraggingBottomLeft;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, selectionTol, size, btmLeft) && allowBottomRightInteraction()) {
        _ms = eDraggingBottomRight;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, selectionTol, size, btmLeft) && allowTopRightInteraction()) {
        _ms = eDraggingTopRight;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, selectionTol, size, btmLeft)  && allowTopLeftInteraction()) {
        _ms = eDraggingTopLeft;
        didSomething = true;
    } else if (isNearbyMidTop(args.penPosition, selectionTol, size, btmLeft) && allowMidTopInteraction()) {
        _ms = eDraggingMidTop;
        didSomething = true;
    } else if (isNearbyMidRight(args.penPosition, selectionTol, size, btmLeft) && allowMidRightInteraction()) {
        _ms = eDraggingMidRight;
        didSomething = true;
    } else if (isNearbyMidBtm(args.penPosition, selectionTol, size, btmLeft)  && allowMidBottomInteraction()) {
        _ms = eDraggingMidBtm;
        didSomething = true;
    } else if (isNearbyMidLeft(args.penPosition, selectionTol, size, btmLeft)  && allowMidLeftInteraction()) {
        _ms = eDraggingMidLeft;
        didSomething = true;
    } else if (isNearbyCenter(args.penPosition, selectionTol, size, btmLeft)  && allowCenterInteraction()) {
        _ms = eDraggingCenter;
        didSomething = true;
    } else {
        _ms = eIdle;
    }
    
    _btmLeftDragPos = btmLeft;
    _sizeDrag = size;
    _lastMousePos = args.penPosition;
    return didSomething;
}

bool RectangleInteract::penUp(const OFX::PenArgs &args)
{
    bool didSmthing = false;
    if (_ms != eIdle) {
        _btmLeft->setValue(_btmLeftDragPos.x, _btmLeftDragPos.y);
        _size->setValue(_sizeDrag.x, _sizeDrag.y);
        didSmthing = true;
    }
    _ms = eIdle;
    return didSmthing;
}

OfxPointD RectangleInteract::getBottomLeft(OfxTime time) const
{
    OfxPointD ret;
    _btmLeft->getValueAtTime(time, ret.x, ret.y);
    return ret;
}
