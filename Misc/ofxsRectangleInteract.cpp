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
#include <cmath>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#define POINT_SIZE 5
#define POINT_TOLERANCE 6




static bool isNearby(const OfxPointD& p1, const OfxPointD& p2, double tolerance, const OfxPointD& pscale)
{
    return std::fabs(p1.x-p2.x) <= tolerance*pscale.x &&  std::fabs(p1.y-p2.y) <= tolerance*pscale.y;
}

static OfxPointD ptopLeft(const OfxPointD& btmLeft, const OfxPointD& size) {
    OfxPointD p;
    p.x = btmLeft.x;
    p.y = btmLeft.y + size.y;
    return p;
}

static OfxPointD ptopRight(const OfxPointD& btmLeft, const OfxPointD& size) {
    OfxPointD p;
    p.x = btmLeft.x + size.x;
    p.y = btmLeft.y + size.y;
    return p;
}

static OfxPointD pbtmLeft(const OfxPointD& btmLeft, const OfxPointD& size) {
    return btmLeft;
}

static OfxPointD pbtmRight(const OfxPointD& btmLeft, const OfxPointD& size) {
    OfxPointD p;
    p.x = btmLeft.x + size.x;
    p.y = btmLeft.y;
    return p;
}

static OfxPointD ptopMid(const OfxPointD& btmLeft, const OfxPointD& size) {
    OfxPointD p;
    p.x = btmLeft.x + size.x/2;
    p.y = btmLeft.y + size.y;
    return p;
}

static OfxPointD pbtmMid(const OfxPointD& btmLeft, const OfxPointD& size) {
    OfxPointD p;
    p.x = btmLeft.x + size.x/2;
    p.y = btmLeft.y;
    return p;
}

static OfxPointD pmidRight(const OfxPointD& btmLeft, const OfxPointD& size) {
    OfxPointD p;
    p.x = btmLeft.x + size.x;
    p.y = btmLeft.y + size.y/2;
    return p;
}

static OfxPointD pmidLeft(const OfxPointD& btmLeft, const OfxPointD& size) {
    OfxPointD p;
    p.x = btmLeft.x;
    p.y = btmLeft.y + size.y/2;
    return p;
}

static OfxPointD pmidMid(const OfxPointD& btmLeft, const OfxPointD& size) {
    OfxPointD p;
    p.x = btmLeft.x + size.x/2;
    p.y = btmLeft.y + size.y/2;
    return p;
}


bool RectangleInteract::draw(const OFX::DrawArgs &args)
{
    OfxPointD btmLeft,size;
    if (_ms != eIdle) {
        btmLeft = _btmLeftDragPos;
        size = _sizeDrag;
    } else {
        btmLeft = getBtmLeft(args.time);
        _size->getValueAtTime(args.time, size.x, size.y);
    }
    
    OfxPointD topLeft  = ptopLeft(btmLeft, size);
    OfxPointD topMid   = ptopMid(btmLeft, size);
    OfxPointD topRight = ptopRight(btmLeft, size);
    OfxPointD midRight = pmidRight(btmLeft, size);
    OfxPointD btmRight = pbtmRight(btmLeft, size);
    OfxPointD btmMid   = pbtmMid(btmLeft, size);
    OfxPointD midLeft  = pmidLeft(btmLeft, size);
    OfxPointD center   = pmidMid(btmLeft, size);

    glColor4f(0.9, 0.9, 0.9, 1.);
    glBegin(GL_LINE_STRIP);
    glVertex2d(btmLeft.x,  btmLeft.y);
    glVertex2d(topLeft.x,  topLeft.y);
    glVertex2d(topRight.x, topRight.y);
    glVertex2d(btmRight.x, btmRight.y);
    glVertex2d(btmLeft.x,  btmLeft.y);
    glEnd();
    
    glPointSize(POINT_SIZE);
    glBegin(GL_POINTS);
    if (_ds == eHoveringBtmLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    if (allowBtmLeftInteraction()) {
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
    if (_ds == eHoveringTopMid) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    if (allowTopMidInteraction()) {
        glVertex2d(topMid.x, topMid.y);
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
    if (_ds == eHoveringBtmRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    if (allowBtmRightInteraction()) {
        glVertex2d(btmRight.x, btmRight.y);
    }
    if (_ds == eHoveringBtmMid) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    if (allowMidBottomInteraction()) {
        glVertex2d(btmMid.x, btmMid.y);
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
    OfxPointD pscale;

    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;

    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;

    OfxPointD size;
    _size->getValueAtTime(args.time, size.x, size.y);
    
    OfxPointD btmLeft = getBtmLeft(args.time);
    bool lastStateWasHovered = _ds != eInactive;
    
    
    aboutToCheckInteractivity(args.time);
    if (isNearby(args.penPosition, ptopLeft(btmLeft,size), POINT_TOLERANCE, pscale) && allowBtmLeftInteraction()) {
        _ds = eHoveringBtmLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, pbtmRight(btmLeft,size), POINT_TOLERANCE, pscale) && allowBtmRightInteraction()) {
        _ds = eHoveringBtmRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, ptopRight(btmLeft,size), POINT_TOLERANCE, pscale) && allowTopRightInteraction()) {
        _ds = eHoveringTopRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, ptopLeft(btmLeft,size), POINT_TOLERANCE, pscale)  && allowTopLeftInteraction()) {
        _ds = eHoveringTopLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, ptopMid(btmLeft,size), POINT_TOLERANCE, pscale) && allowTopMidInteraction()) {
        _ds = eHoveringTopMid;
        didSomething = true;
    } else if (isNearby(args.penPosition, pmidRight(btmLeft,size), POINT_TOLERANCE, pscale) && allowMidRightInteraction()) {
        _ds = eHoveringMidRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, pbtmMid(btmLeft,size), POINT_TOLERANCE, pscale)  && allowMidBottomInteraction()) {
        _ds = eHoveringBtmMid;
        didSomething = true;
    } else if (isNearby(args.penPosition, pmidLeft(btmLeft,size), POINT_TOLERANCE, pscale)  && allowMidLeftInteraction()) {
        _ds = eHoveringMidLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, pmidMid(btmLeft,size), POINT_TOLERANCE, pscale)  && allowCenterInteraction()) {
        _ds = eHoveringCenter;
        didSomething = true;
    } else {
        _ds = eInactive;
    }
    
    if (_ms == eDraggingBtmLeft) {
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
    } else if (_ms == eDraggingBtmRight) {
        OfxPointD topLeft;
        topLeft.x = _btmLeftDragPos.x;
        topLeft.y = _btmLeftDragPos.y + _sizeDrag.y;
        _sizeDrag.x += delta.x;
        _btmLeftDragPos.y += delta.y;
        _sizeDrag.y = topLeft.y - _btmLeftDragPos.y;
        didSomething = true;
    } else if (_ms == eDraggingTopMid) {
        _sizeDrag.y += delta.y;
        didSomething = true;
    } else if (_ms == eDraggingMidRight) {
        _sizeDrag.x += delta.x;
        didSomething = true;
    } else if (_ms == eDraggingBtmMid) {
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
        if (_ms == eDraggingBtmLeft) {
            _ms = eDraggingBtmRight;
        } else if (_ms == eDraggingMidLeft) {
            _ms = eDraggingMidRight;
        } else if (_ms == eDraggingTopLeft) {
            _ms = eDraggingTopRight;
        } else if (_ms == eDraggingBtmRight) {
            _ms = eDraggingBtmLeft;
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
            _ms = eDraggingBtmLeft;
        } else if (_ms == eDraggingTopMid) {
            _ms = eDraggingBtmMid;
        } else if (_ms == eDraggingTopRight) {
            _ms = eDraggingBtmRight;
        } else if (_ms == eDraggingBtmLeft) {
            _ms = eDraggingTopLeft;
        } else if (_ms == eDraggingBtmMid) {
            _ms = eDraggingTopMid;
        } else if (_ms == eDraggingBtmRight) {
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
    OfxPointD pscale;

    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;

    OfxPointD size;
    _size->getValueAtTime(args.time, size.x, size.y);
    
    
    OfxPointD btmLeft = getBtmLeft(args.time);
    
    aboutToCheckInteractivity(args.time);
    
    if (isNearby(args.penPosition, pbtmLeft(btmLeft,size), POINT_TOLERANCE, pscale) && allowBtmLeftInteraction()) {
        _ms = eDraggingBtmLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, pbtmRight(btmLeft,size), POINT_TOLERANCE, pscale) && allowBtmRightInteraction()) {
        _ms = eDraggingBtmRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, ptopRight(btmLeft,size), POINT_TOLERANCE, pscale) && allowTopRightInteraction()) {
        _ms = eDraggingTopRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, ptopLeft(btmLeft,size), POINT_TOLERANCE, pscale) && allowTopLeftInteraction()) {
        _ms = eDraggingTopLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, ptopMid(btmLeft,size), POINT_TOLERANCE, pscale) && allowTopMidInteraction()) {
        _ms = eDraggingTopMid;
        didSomething = true;
    } else if (isNearby(args.penPosition, pmidRight(btmLeft,size), POINT_TOLERANCE, pscale) && allowMidRightInteraction()) {
        _ms = eDraggingMidRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, pbtmMid(btmLeft,size), POINT_TOLERANCE, pscale) && allowMidBottomInteraction()) {
        _ms = eDraggingBtmMid;
        didSomething = true;
    } else if (isNearby(args.penPosition, pmidLeft(btmLeft,size), POINT_TOLERANCE, pscale) && allowMidLeftInteraction()) {
        _ms = eDraggingMidLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, pmidMid(btmLeft,size), POINT_TOLERANCE, pscale) && allowCenterInteraction()) {
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

OfxPointD RectangleInteract::getBtmLeft(OfxTime time) const
{
    OfxPointD ret;
    _btmLeft->getValueAtTime(time, ret.x, ret.y);
    return ret;
}
