/*
 OFX generic position interact.

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
#ifndef __Misc__ofxsPositionInteract__
#define __Misc__ofxsPositionInteract__

#include <ofxsInteract.h>
#include <ofxsImageEffect.h>

namespace OFX {

/// template for a generic position interact.
/*
 The PositionInteractParam class must define a static name() function, returning the OFX parameter name.
 (using const char* directly as template parameter is not reliable) :
 
 struct MyPositionInteractParam {
     static const char *name() { return kMyName; }
 };

// The position param should be defined as follows:
Double2DParamDescriptor* position = desc.defineDouble2DParam(kMyName);
position->setLabels(kMyLabel, kMyLabel, kMyLabel);
position->setHint(kMyHint);
position->setDoubleType(eDoubleTypeXYAbsolute);
position->setDefaultCoordinateSystem(eCoordinatesNormalised);
position->setDefault(0.5, 0.5);
page->addChild(*position);
*/
template<typename PositionInteractParam>
class PositionInteract : public OFX::OverlayInteract
{
public:
    PositionInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _state(eInActive)
    {
        _position = effect->fetchDouble2DParam(PositionInteractParam::name());
        assert(_position);
    }

private:
    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args);
    virtual bool penMotion(const OFX::PenArgs &args);
    virtual bool penDown(const OFX::PenArgs &args);
    virtual bool penUp(const OFX::PenArgs &args);
    OfxPointD getCanonicalPosition(double time) const
    {
        OfxPointD offset = _effect->getProjectOffset();
        OfxPointD size = _effect->getProjectSize();
        double x,y;
        _position->getValueAtTime(time, x, y);
        OfxPointD retVal;
        retVal.x = x * size.x + offset.x;
        retVal.y = y * size.y + offset.y;
        return retVal;
    }
    void setCanonicalPosition(double x, double y)
    {
        OfxPointD offset = _effect->getProjectOffset();
        OfxPointD size = _effect->getProjectSize();
        _position->setValue((x - offset.x) / size.x, (y - offset.y) / size.y);
    }

private:
    enum StateEnum {
        eInActive,
        ePoised,
        ePicked
    };

    StateEnum _state;
    OFX::Double2DParam* _position;

    double xHairSize() const { return 5; }
};

template <typename ParamName>
bool PositionInteract<ParamName>::draw(const OFX::DrawArgs &args)
{
    if (!_position) {
        return false; // nothing to draw
    }

    OfxPointD pscale;
    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;

    OfxRGBColourF col;
    switch (_state) {
        case eInActive : col.r = col.g = col.b = 0.0f; break;
        case ePoised   : col.r = col.g = col.b = 0.5f; break;
        case ePicked   : col.r = col.g = col.b = 0.8f; break;
    }

    // make the box a constant size on screen by scaling by the pixel scale
    float dx = (float)(xHairSize() / args.pixelScale.x);
    float dy = (float)(xHairSize() / args.pixelScale.y);

    OfxPointD pos = getCanonicalPosition(args.time);


    glPushAttrib(GL_ALL_ATTRIB_BITS);
    //glDisable(GL_LINE_STIPPLE);
    glEnable(GL_LINE_SMOOTH);
    //glEnable(GL_POINT_SMOOTH);
    glEnable(GL_BLEND);
    glHint(GL_LINE_SMOOTH_HINT,GL_DONT_CARE);
    glLineWidth(1.5);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    glPushMatrix();
    glTranslated(pos.x, pos.y, 0);
    // Draw everything twice
    // l = 0: shadow
    // l = 1: drawing
    for (int l = 0; l < 2; ++l) {
        if (l == 0) {
            // Draw a shadow for the cross hair
            // shift by (1,1) pixel
            glTranslated(pscale.x, -pscale.y, 0);
            glColor3f(0., 0., 0.);
        } else {
            glColor3f(col.r, col.g, col.b);
        }
        glBegin(GL_LINES);
        glVertex2f(-dx, 0);
        glVertex2f(dx, 0);
        glVertex2f(0, -dy);
        glVertex2f(0, dy);
        glEnd();
        if (l == 0) {
            glTranslated(-pscale.x, pscale.y, 0);
        }
    }
    glPopMatrix();

    glPopAttrib();

    return true;
}

// overridden functions from OFX::Interact to do things
template <typename ParamName>
bool PositionInteract<ParamName>::penMotion(const OFX::PenArgs &args)
{
    if (!_position) {
        return false;
    }
    // figure the size of the box in cannonical coords
    float dx = (float)(xHairSize() / args.pixelScale.x);
    float dy = (float)(xHairSize() / args.pixelScale.y);

    OfxPointD pos = getCanonicalPosition(args.time);

    // pen position is in cannonical coords
    OfxPointD penPos = args.penPosition;

    switch (_state)
    {
        case eInActive :
        case ePoised   :
        {
            // are we in the box, become 'poised'
            StateEnum newState;
            penPos.x -= pos.x;
            penPos.y -= pos.y;
            if (std::labs(penPos.x) < dx &&
                std::labs(penPos.y) < dy) {
                newState = ePoised;
            }
            else {
                newState = eInActive;
            }

            if (_state != newState) {
                _state = newState;
                _effect->redrawOverlays();
            }
        }
            break;

        case ePicked   :
        {
            setCanonicalPosition(penPos.x, penPos.y);
            _effect->redrawOverlays();
        }
            break;
    }
    return _state != eInActive;
}

template <typename ParamName>
bool PositionInteract<ParamName>::penDown(const OFX::PenArgs &args)
{
    if (!_position) {
        return false;
    }
    penMotion(args);
    if (_state == ePoised) {
        _state = ePicked;
        setCanonicalPosition(args.penPosition.x, args.penPosition.y);
        _effect->redrawOverlays();
    }

    return _state == ePicked;
}

template <typename ParamName>
bool PositionInteract<ParamName>::penUp(const OFX::PenArgs &args)
{
    if (!_position) {
        return false;
    }
    if (_state == ePicked)
    {
        _state = ePoised;
        penMotion(args);
        _effect->redrawOverlays();
        return true;
    }
    return false;
}

template <typename ParamName>
class PositionOverlayDescriptor : public OFX::DefaultEffectOverlayDescriptor<PositionOverlayDescriptor<ParamName>, PositionInteract<ParamName> > {};

} // namespace OFX

#endif /* defined(__Misc__ofxsPositionInteract__) */
