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
#define CROSS_SIZE 7
#define HANDLE_SIZE 6

using OFX::RectangleInteract;

static bool isNearby(const OfxPointD& p, double x, double y, double tolerance, const OfxPointD& pscale)
{
    return std::fabs(p.x-x) <= tolerance*pscale.x &&  std::fabs(p.y-y) <= tolerance*pscale.y;
}

static void drawPoint(bool draw, double x, double y, RectangleInteract::DrawState id, RectangleInteract::DrawState ds, int foreground)
{
    if (draw) {
        if (foreground == 1) {
            if (ds == id) {
                glColor3f(0., 1., 0.);
            } else {
                glColor3f(0.8, 0.8, 0.8);
            }
        }
        glVertex2d(x, y);
    }
}

bool RectangleInteract::draw(const OFX::DrawArgs &args)
{
    OfxPointD pscale;
    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;

    double x1, y1, w, h;
    if (_ms != eIdle) {
        x1 = _btmLeftDragPos.x;
        y1 =_btmLeftDragPos.y;
        w = _sizeDrag.x;
        h = _sizeDrag.y;
    } else {
        _btmLeft->getValueAtTime(args.time, x1, y1);
        _size->getValueAtTime(args.time, w, h);
    }
    double x2 = x1 + w;
    double y2 = y1 + h;
    double xc = x1 + w/2;
    double yc = y1 + h/2;

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
        glVertex2d(x1, y1);
        glVertex2d(x1, y2);
        glVertex2d(x2, y2);
        glVertex2d(x2, y1);
        glVertex2d(x1, y1);
        glEnd();

        glPointSize(POINT_SIZE);
        glBegin(GL_POINTS);
        drawPoint(allowBtmLeftInteraction(),  x1, y1, eHoveringBtmLeft,  _ds, l);
        drawPoint(allowMidLeftInteraction(),  x1, yc, eHoveringMidLeft,  _ds, l);
        drawPoint(allowTopLeftInteraction(),  x1, y2, eHoveringTopLeft,  _ds, l);
        drawPoint(allowBtmMidInteraction(),   xc, y1, eHoveringBtmMid,   _ds, l);
        drawPoint(allowCenterInteraction(),   xc, yc, eHoveringCenter,   _ds, l);
        drawPoint(allowTopMidInteraction(),   xc, y2, eHoveringTopMid,   _ds, l);
        drawPoint(allowBtmRightInteraction(), x2, y1, eHoveringBtmRight, _ds, l);
        drawPoint(allowMidRightInteraction(), x2, yc, eHoveringMidRight, _ds, l);
        drawPoint(allowTopRightInteraction(), x2, y2, eHoveringTopRight, _ds, l);
        glEnd();
        glPointSize(1);

        ///draw center cross hair
        glBegin(GL_LINES);
        if (l == 1) {
            if (_ds == eHoveringCenter) {
                glColor3f(0., 1., 0.);
            } else if (!allowCenterInteraction()) {
                glColor3f(0.5, 0.5, 0.5);
            } else {
                glColor3f(0.8, 0.8, 0.8);
            }
        }
        glVertex2d(xc - CROSS_SIZE*pscale.x, yc);
        glVertex2d(xc + CROSS_SIZE*pscale.x, yc);
        glVertex2d(xc, yc - CROSS_SIZE*pscale.y);
        glVertex2d(xc, yc + CROSS_SIZE*pscale.y);
        glEnd();
        if (l == 0) {
            // translate (-1,1) pixels
            glTranslated(-pscale.x, pscale.y, 0);
        }
    }
    glPopAttrib();
    return true;
}

bool RectangleInteract::penMotion(const OFX::PenArgs &args)
{
    OfxPointD pscale;
    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;

    double x1, y1, w, h;
    if (_ms != eIdle) {
        x1 = _btmLeftDragPos.x;
        y1 =_btmLeftDragPos.y;
        w = _sizeDrag.x;
        h = _sizeDrag.y;
    } else {
        _btmLeft->getValueAtTime(args.time, x1, y1);
        _size->getValueAtTime(args.time, w, h);
    }
    double x2 = x1 + w;
    double y2 = y1 + h;
    double xc = x1 + w/2;
    double yc = y1 + h/2;

    bool didSomething = false;

    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;

    bool lastStateWasHovered = _ds != eInactive;
    

    aboutToCheckInteractivity(args.time);
    // test center first
    if (       isNearby(args.penPosition, xc, yc, POINT_TOLERANCE, pscale)  && allowCenterInteraction()) {
        _ds = eHoveringCenter;
        didSomething = true;
    } else if (isNearby(args.penPosition, x1, y1, POINT_TOLERANCE, pscale) && allowBtmLeftInteraction()) {
        _ds = eHoveringBtmLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, x2, y1, POINT_TOLERANCE, pscale) && allowBtmRightInteraction()) {
        _ds = eHoveringBtmRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, x1, y2, POINT_TOLERANCE, pscale)  && allowTopLeftInteraction()) {
        _ds = eHoveringTopLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, x2, y2, POINT_TOLERANCE, pscale) && allowTopRightInteraction()) {
        _ds = eHoveringTopRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, xc, y1, POINT_TOLERANCE, pscale)  && allowBtmMidInteraction()) {
        _ds = eHoveringBtmMid;
        didSomething = true;
    } else if (isNearby(args.penPosition, xc, y2, POINT_TOLERANCE, pscale) && allowTopMidInteraction()) {
        _ds = eHoveringTopMid;
        didSomething = true;
    } else if (isNearby(args.penPosition, x1, yc, POINT_TOLERANCE, pscale)  && allowMidLeftInteraction()) {
        _ds = eHoveringMidLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, x2, yc, POINT_TOLERANCE, pscale) && allowMidRightInteraction()) {
        _ds = eHoveringMidRight;
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
    OfxPointD pscale;
    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;

    double x1, y1, w, h;
    if (_ms != eIdle) {
        x1 = _btmLeftDragPos.x;
        y1 =_btmLeftDragPos.y;
        w = _sizeDrag.x;
        h = _sizeDrag.y;
    } else {
        _btmLeft->getValueAtTime(args.time, x1, y1);
        _size->getValueAtTime(args.time, w, h);
    }
    double x2 = x1 + w;
    double y2 = y1 + h;
    double xc = x1 + w/2;
    double yc = y1 + h/2;

    bool didSomething = false;

    aboutToCheckInteractivity(args.time);
    
    // test center first
    if (       isNearby(args.penPosition, xc, yc, POINT_TOLERANCE, pscale)  && allowCenterInteraction()) {
        _ms = eDraggingCenter;
        didSomething = true;
    } else if (isNearby(args.penPosition, x1, y1, POINT_TOLERANCE, pscale) && allowBtmLeftInteraction()) {
        _ms = eDraggingBtmLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, x2, y1, POINT_TOLERANCE, pscale) && allowBtmRightInteraction()) {
        _ms = eDraggingBtmRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, x1, y2, POINT_TOLERANCE, pscale)  && allowTopLeftInteraction()) {
        _ms = eDraggingTopLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, x2, y2, POINT_TOLERANCE, pscale) && allowTopRightInteraction()) {
        _ms = eDraggingTopRight;
        didSomething = true;
    } else if (isNearby(args.penPosition, xc, y1, POINT_TOLERANCE, pscale)  && allowBtmMidInteraction()) {
        _ms = eDraggingBtmMid;
        didSomething = true;
    } else if (isNearby(args.penPosition, xc, y2, POINT_TOLERANCE, pscale) && allowTopMidInteraction()) {
        _ms = eDraggingTopMid;
        didSomething = true;
    } else if (isNearby(args.penPosition, x1, yc, POINT_TOLERANCE, pscale)  && allowMidLeftInteraction()) {
        _ms = eDraggingMidLeft;
        didSomething = true;
    } else if (isNearby(args.penPosition, x2, yc, POINT_TOLERANCE, pscale) && allowMidRightInteraction()) {
        _ms = eDraggingMidRight;
        didSomething = true;
    } else {
        _ms = eIdle;
    }
    
    _btmLeftDragPos.x = x1;
    _btmLeftDragPos.y = y1;
    _sizeDrag.x = w;
    _sizeDrag.y = h;
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
