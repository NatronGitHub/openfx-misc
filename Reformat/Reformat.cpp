/*
 OFX Roto plugin.
 
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
 
 
 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.
 
 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England
 
 */

/*
   Although the indications from nuke/fnOfxExtensions.h were followed, and the
   kFnOfxImageEffectActionGetTransform action was implemented in the Support
   library, that action is never called by the Nuke host, so it cannot be tested.
   The code is left here for reference or for further extension.

   There is also an open question about how the last plugin in a transform chain
   may get the concatenated transform from upstream, the untransformed source image,
   concatenate its own transform and apply the resulting transform in its render
   action. Should the host be doing this instead?
*/
// Uncomment the following to enable the experimental host transform code.
//#define ENABLE_HOST_TRANSFORM

#include "Reformat.h"

#include <cmath>
#include <climits>

#include "ofxsProcessing.H"
#include "ofxsTransform3x3.h"


#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ofxsOGLTextRenderer.h"

#define kTypeParamName "Type"
#define kFormatParamName "Output format"
#define kWidthParamName "Width"
#define kHeightParamName "Height"
#define kParParamName "Pixel aspect ratio"
#define kScaleParamName "Scale"
#define kFixedSizeParamName "Fixed size"
#define kResizeTypeParamName "Resize Type"
#define kCenterParamName "Center"
#define kFlipParamName "Flip"
#define kFlopParamName "Flop"


using namespace OFX;

namespace  {

static void getSizesFromFormatIndex(int index,int* width,int* height,double* par)
{
    switch (index) {
        case 0:
            *width = 640;
            *height = 480;
            *par = 1.;
            break;
        case 1:
            *width = 720;
            *height = 486;
            *par = 0.91;
            break;
        case 2:
            *width = 720;
            *height = 576;
            *par = 1.09;
            break;
        case 3:
            *width = 1920;
            *height = 1080;
            *par = 1.;
            break;
        case 4:
            *width = 720;
            *height = 486;
            *par = 1.21;
            break;
        case 5:
            *width = 720;
            *height = 576;
            *par = 1.46;
            break;
        case 6:
            *width = 1024;
            *height = 778;
            *par = 1.;
            break;
        case 7:
            *width = 914;
            *height = 778;
            *par = 2.;
            break;
        case 8:
            *width = 2048;
            *height = 1556;
            *par = 1.;
            break;
        case 9:
            *width = 1828;
            *height = 1556;
            *par = 2.;
            break;
        case 10:
            *width = 4096;
            *height = 3112;
            *par = 1.;
            break;
        case 11:
            *width = 3656;
            *height = 3112;
            *par = 2.;
            break;
        case 12:
            *width = 256;
            *height = 256;
            *par = 1.;
            break;
        case 13:
            *width = 512;
            *height = 512;
            *par = 1.;
            break;
        case 14:
            *width = 1024;
            *height = 1024;
            *par = 1.;
            break;
        case 15:
            *width = 2048;
            *height = 2048;
            *par = 1.;
            break;
        default:
            assert(false);
            break;
    }
}
    
}



////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ReformatPlugin : public Transform3x3Plugin
{
protected:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    
    ChoiceParam* _type;
    ChoiceParam* _format;
    IntParam* _width;
    IntParam* _height;
    BooleanParam* _fixedSize;
    DoubleParam* _par;
    Double2DParam* _scale;
    ChoiceParam* _resizeType;
    Double2DParam* _center;
    BooleanParam* _flip;
    BooleanParam* _flop;
    
public:
    /** @brief ctor */
    ReformatPlugin(OfxImageEffectHandle handle)
    : Transform3x3Plugin(handle,false)
    , dstClip_(0)
    , srcClip_(0)
    , _type(0)
    , _format(0)
    , _width(0)
    , _height(0)
    , _fixedSize(0)
    , _par(0)
    , _scale(0)
    , _resizeType(0)
    , _center(0)
    , _flip(0)
    , _flop(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA);
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA);
        
        _type = fetchChoiceParam(kTypeParamName);
        _format = fetchChoiceParam(kFormatParamName);
        _width = fetchIntParam(kWidthParamName);
        _height = fetchIntParam(kHeightParamName);
        _fixedSize = fetchBooleanParam(kFixedSizeParamName);
        _par = fetchDoubleParam(kParParamName);
        _scale = fetchDouble2DParam(kScaleParamName);
        _resizeType = fetchChoiceParam(kResizeTypeParamName);
        _center = fetchDouble2DParam(kCenterParamName);
        _flip = fetchBooleanParam(kFlipParamName);
        _flop = fetchBooleanParam(kFlopParamName);
        
        assert(_type && _format && _width && _height && _fixedSize && _par && _scale && _resizeType && _center && _flop && _flip);
        
    }
    
    virtual bool isIdentity(double time) /*OVERRIDE FINAL*/;
    
    virtual bool getInverseTransformCanonical(double time, bool invert,Transform3x3Plugin::eGetTransformReason reason,
                                              OFX::Matrix3x3* invtransform) /*OVERRIDE FINAL*/;
    
private:
    
   

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) /*OVERRIDE FINAL*/;
};



bool ReformatPlugin::getInverseTransformCanonical(OfxTime time, bool invert,Transform3x3Plugin::eGetTransformReason reason,
                                                  OFX::Matrix3x3* invtransform)
{
    
    double scaleX = INT_MIN,scaleY= INT_MIN;
    int type;
    _type->getValue(type);
    
    int resizeType;
    _resizeType->getValue(resizeType);
    
    bool fixedSize = true;
    
    OfxRectD srcRod = srcClip_->getRegionOfDefinition(time);
    double srcW = srcRod.x2 - srcRod.x1;
    double srcH = srcRod.y2 - srcRod.y1;
    int w,h;
    double dstPar;
    switch (type) {
        case 0:
        {
            int formatIndex;
            _format->getValue(formatIndex);
            getSizesFromFormatIndex(formatIndex, &w, &h, &dstPar);
            scaleX = (double)w / srcW ;
            scaleY = (double)h / srcH / dstPar;
        }   break;
        case 1:
        {
            _width->getValue(w);
            _height->getValue(h);
            _par->getValue(dstPar);
            _fixedSize->getValue(fixedSize);
            scaleX = (double)w / srcW ;
            scaleY = (double)h / srcH / dstPar;
        }   break;
        case 2:
        {
            _scale->getValue(scaleX, scaleY);
            w = scaleX * srcW;
            h = scaleY * srcH;
            dstPar = 1.;
        }   break;
        default:
            assert(false);
            break;
    }
    
    assert(dstPar != 0.);
    
    // double scaleRatio = scaleX / scaleY;
    
    if (resizeType == 0) {
        ///This is just a crop, pixels aren't transformed.
        if(!fixedSize || reason == Transform3x3Plugin::eGetTransformRender) {
            scaleX = 1.;
            scaleY = 1. / dstPar;
        }
    } else if (resizeType == 1) {
        
        if(!fixedSize || reason == Transform3x3Plugin::eGetTransformRender) {
            scaleY = scaleX / dstPar;
        }
        
    } else if (resizeType == 2) {
        
        if(!fixedSize || reason == Transform3x3Plugin::eGetTransformRender) {
            scaleX = scaleY;
        }
        
    } else if (resizeType == 3) {
        if (srcW <= srcH) {
            if (!fixedSize || reason == Transform3x3Plugin::eGetTransformRender) {
                scaleX = scaleY;
            }
        } else {
            if (!fixedSize || reason == Transform3x3Plugin::eGetTransformRender) {
                scaleY = scaleX / dstPar;
            }
        }
    } else if (resizeType == 4) {
        if (srcW >= srcH) {
            if (!fixedSize || reason == Transform3x3Plugin::eGetTransformRender) {
                scaleX = scaleY;
            }
            
        } else {
            if (!fixedSize  || reason == Transform3x3Plugin::eGetTransformRender) {
                scaleY = scaleX / dstPar;
            }
        }
    }
    
    double centerX,centerY;
    _center->getValueAtTime(time, centerX, centerY);
    
    bool flip,flop;
    _flip->getValue(flip);
    _flop->getValue(flop);

    if (!invert) {
        *invtransform = OFX::ofxsMatInverseTransformCanonical(0., 0., scaleX, scaleY, 0., 0., false, 0., centerX, centerY);
    } else {
        *invtransform = OFX::ofxsMatTransformCanonical(0., 0., scaleX, scaleY, 0., 0., false, 0., centerX, centerY);
    }
    
    if (flip && flop) {
        *invtransform = *invtransform * OFX::ofxsMatScaleAroundPoint(-1, -1,centerX,centerY);
    } else if (flip && !flop) {
        *invtransform = *invtransform * OFX::ofxsMatScaleAroundPoint(1, -1,centerX,centerY);
    } else if (!flip && flop) {
        *invtransform = *invtransform * OFX::ofxsMatScaleAroundPoint(-1, 1,centerX,centerY);
    }
    
    return true;
}


// overridden is identity
bool ReformatPlugin::isIdentity(double time)
{
    int type;
    _type->getValue(type);
    
    bool flip,flop;
    _flip->getValue(flip);
    if (flip) {
        return false;
    }
    
    _flop->getValue(flop);
    if (flop) {
        return false;
    }
    
    switch (type) {
        case 0:
        {
            OfxRectD rod = srcClip_->getRegionOfDefinition(time);
            double srcPar = srcClip_->getPixelAspectRatio();
            int formatIndex;
            _format->getValue(formatIndex);
            int width,height;
            double par;
            getSizesFromFormatIndex(formatIndex, &width, &height, &par);
            if (width == (rod.x2 - rod.x1) && height == (rod.y2 - rod.y1) && par == srcPar) {
                return true;
            }
        }
            break;
        case 1:
        {
            OfxRectD rod = srcClip_->getRegionOfDefinition(time);
            double srcPar = srcClip_->getPixelAspectRatio();

            int width,height;
            double par;
            _width->getValue(width);
            _height->getValue(height);
            _par->getValue(par);
            if (width == (rod.x2 - rod.x1) && height == (rod.y2 - rod.y1) && par == srcPar) {
                return true;
            }
        }  break;

        case 2:
        {
            double scaleX,scaleY;
            _scale->getValue(scaleX,scaleY);
            if (scaleX == 1. && scaleY == 1.) {
                return true;
            }
        }   break;

        default:
            assert(false);
            break;
    }
    return false;
}

void ReformatPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kTypeParamName) {
        int index;
        _type->getValue(index);
        switch (index) {
            case 0:
                _format->setIsSecret(false);
                _width->setIsSecret(true);
                _height->setIsSecret(true);
                _fixedSize->setIsSecret(true);
                _par->setIsSecret(true);
                _scale->setIsSecret(true);
                break;
            case 1:
                _format->setIsSecret(true);
                _width->setIsSecret(false);
                _height->setIsSecret(false);
                _fixedSize->setIsSecret(false);
                _par->setIsSecret(false);
                _scale->setIsSecret(true);
                break;
            case 2:
                _format->setIsSecret(true);
                _width->setIsSecret(true);
                _height->setIsSecret(true);
                _fixedSize->setIsSecret(true);
                _par->setIsSecret(true);
                _scale->setIsSecret(false);
                break;
            default:
                break;
        }
    } else if (paramName == kResizeTypeParamName) {
        int index;
        _resizeType->getValue(index);
        switch (index) {
            case 0:
            case 3:
            case 4:
            case 5:
                _width->setEnabled(true);
                _height->setEnabled(true);
                break;
            case 1:
                _width->setEnabled(true);
                _height->setEnabled(false);
                break;
            case 2:
                _width->setEnabled(false);
                _height->setEnabled(true);
                break;
            default:
                break;
        }
    }
}


class ReformatTransformInteract : public OFX::OverlayInteract
{
    
    enum MouseState
    {
        eIdle = 0,
        eDraggingCenter
    };
    
    enum DrawState
    {
        eInactive = 0,
        eHoveringCenter
    };
    
public:
    
    ReformatTransformInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _ms(eIdle)
    , _ds(eInactive)
    , _lastMousePos()
    , _centerDragPos()
    , _invert(0)
    , _center(0)
    {

        _center = effect->fetchDouble2DParam(kCenterParamName);
        _invert = effect->fetchBooleanParam(kTransform3x3InvertParamName);
        assert(_center && _invert);
        addParamToSlaveTo(_center);
        addParamToSlaveTo(_invert);
    }
    
    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args);
    virtual bool penMotion(const OFX::PenArgs &args);
    virtual bool penDown(const OFX::PenArgs &args);
    virtual bool penUp(const OFX::PenArgs &args);

    
private:
    
    bool isNearbyCenter(const OfxPointD& pos,double tolerance)
    {
        OfxPointD center;
        _center->getValue(center.x, center.y);
        if (pos.x >= (center.x - tolerance) && pos.x <= (center.x + tolerance) &&
            pos.y >= (center.y - tolerance) && pos.y <= (center.y + tolerance)) {
            return true;
        } else {
            return false;
        }
    }
    
    MouseState _ms;
    DrawState _ds;
    
    OfxPointD _lastMousePos;
    OfxPointD _centerDragPos;
    
    OFX::BooleanParam* _invert;
    OFX::Double2DParam* _center;
};

bool ReformatTransformInteract::draw(const OFX::DrawArgs &args)
{
    OfxPointD center;
    if (_ms == eDraggingCenter) {
        center = _centerDragPos;
    } else {
        _center->getValue(center.x, center.y);
    }
    
    glPointSize(5);
    glBegin(GL_POINTS);
    
    if (_ds == eHoveringCenter || _ms == eDraggingCenter) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1., 1., 1., 1.);
    }
    glVertex2d(center.x, center.y);
    glEnd();
    
    glPointSize(1);
    
    double offset = 20 * args.pixelScale.x;
    TextRenderer::bitmapString(center.x - offset,center.y - offset ,kCenterParamName);
    bool inverted;
    _invert->getValue(inverted);
    if (inverted) {
        double arrowXPosition = 0;
        double arrowXHalfSize = 10 * args.pixelScale.x;
        double arrowHeadOffsetX = 3 * args.pixelScale.x;
        double arrowHeadOffsetY = 3 * args.pixelScale.x;
        
        glPushMatrix ();
        glTranslatef (arrowXPosition, 0., 0);
        
        glBegin(GL_LINES);
        ///draw the central bar
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize, 0.);
        
        ///left triangle
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, arrowHeadOffsetY);
        
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, -arrowHeadOffsetY);
        
        ///right triangle
        glVertex2d(+ arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, arrowHeadOffsetY);
        
        glVertex2d(+ arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, -arrowHeadOffsetY);
        glEnd();
        
        glRotated(90., 0., 0., 1.);
        
        glBegin(GL_LINES);
        ///draw the central bar
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize, 0.);
        
        ///left triangle
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, arrowHeadOffsetY);
        
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, -arrowHeadOffsetY);
        
        ///right triangle
        glVertex2d(+ arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, arrowHeadOffsetY);
        
        glVertex2d(+ arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, -arrowHeadOffsetY);
        glEnd();
        
        glPopMatrix ();
    }

    
    return true;
}

bool ReformatTransformInteract::penMotion(const OFX::PenArgs &args)
{
    bool didSomething = true;
    double selectionTol = 15. * args.pixelScale.x;
    if (isNearbyCenter(args.penPosition, selectionTol)) {
        _ds = eHoveringCenter;
    } else {
        _ds = eInactive;
    }
    
    if (_ms == eDraggingCenter) {
        _centerDragPos = args.penPosition;
    }
    _lastMousePos = args.penPosition;
    return didSomething;
}

bool ReformatTransformInteract::penDown(const OFX::PenArgs &args)
{
    bool didSomething = false;
    double selectionTol = 15. * args.pixelScale.x;
    if (isNearbyCenter(args.penPosition, selectionTol)) {
        _ms = eDraggingCenter;
        _centerDragPos = args.penPosition;
        didSomething = true;
    }
    _lastMousePos = args.penPosition;
    return didSomething;
}

bool ReformatTransformInteract::penUp(const OFX::PenArgs &args)
{
    if (_ms == eDraggingCenter) {
        _center->setValue(_centerDragPos.x, _centerDragPos.y);
    }
    _ms = eIdle;
    return true;
}

class ReformatOverlayDescriptor : public DefaultEffectOverlayDescriptor<ReformatOverlayDescriptor, ReformatTransformInteract> {};


void ReformatPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("ReformatOFX", "ReformatOFX", "ReformatOFX");
    desc.setPluginGrouping("Transform");
    desc.setPluginDescription("Converts one image format to another.");
    
    Transform3x3Describe(desc, false);
    desc.setOverlayInteractDescriptor(new ReformatOverlayDescriptor);
}



OFX::ImageEffect* ReformatPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new ReformatPlugin(handle);
}


static void
ReformatPluginDescribeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, PageParamDescriptor *page)
{
    // NON-GENERIC PARAMETERS
    //
    ChoiceParamDescriptor* type = desc.defineChoiceParam(kTypeParamName);
    type->setLabels(kTypeParamName, kTypeParamName, kTypeParamName);
    type->appendOption("Format");
    type->appendOption("Size");
    type->appendOption("Scale");
    type->setDefault(0);
    type->setAnimates(false);
    type->setHint("Chooses how to resize the input image:\n"
                  "Format: Convert the input image to fit into the output format\n"
                  "Size: Scales the input image to fit into or fill a precise size\n"
                  "Scale: Scales the image by a given factor, the scale will be adjusted by the direction chosen by the resize type parameter.");
    page->addChild(*type);
    
    ChoiceParamDescriptor* outputFormat = desc.defineChoiceParam(kFormatParamName);
    outputFormat->setLabels(kFormatParamName, kFormatParamName, kFormatParamName);
    outputFormat->appendOption("PC Video                 640x480   1.");
    outputFormat->appendOption("NTSC                     720x486   0.91.");
    outputFormat->appendOption("PAL                      720x576   1.09");
    outputFormat->appendOption("HD                       1920x1080 1.");
    outputFormat->appendOption("NTSC 16:9                720x486   1.21");
    outputFormat->appendOption("PAL 16:9                 720x576   1.46");
    outputFormat->appendOption("1K super 35(full ap)     1024x778  1.");
    outputFormat->appendOption("1K Cinemascope           914x778   2.");
    outputFormat->appendOption("2K super 35(full ap)     2048x1556 1.");
    outputFormat->appendOption("2K Cinemascope           1828x1556 2.");
    outputFormat->appendOption("4K super 35(full ap)     4096x3112 1.");
    outputFormat->appendOption("4K Cinemascope           3656x3112 2.");
    outputFormat->appendOption("Square 256               256x256   1.");
    outputFormat->appendOption("Square 512               512x512   1.");
    outputFormat->appendOption("Square 1K                1024x1024 1.");
    outputFormat->appendOption("Square 2K                2048x2048 1.");
    outputFormat->setDefault(0);
    outputFormat->setHint("The output format of the image.");
    outputFormat->setAnimates(false);
    page->addChild(*outputFormat);
    
    IntParamDescriptor* width = desc.defineIntParam(kWidthParamName);
    width->setLabels(kWidthParamName, kWidthParamName, kWidthParamName);
    width->setHint("The width of the output image.");
    width->setDefault(200);
    width->setRange(1, INT_MAX);
    width->setAnimates(false);
    width->setIsSecret(true);
    width->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*width);
    
    IntParamDescriptor* height = desc.defineIntParam(kHeightParamName);
    height->setLabels(kHeightParamName, kHeightParamName, kHeightParamName);
    height->setHint("The height of the output image.");
    height->setDefault(200);
    height->setRange(1, INT_MAX);
    height->setAnimates(false);
    height->setIsSecret(true);
    height->setEnabled(false);
    height->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*height);
    
    BooleanParamDescriptor* fixedSize = desc.defineBooleanParam(kFixedSizeParamName);
    fixedSize->setLabels(kFixedSizeParamName,kFixedSizeParamName,kFixedSizeParamName);
    fixedSize->setDefault(false);
    fixedSize->setHint("When checked, the output will be forced to match exactly the provided size with one direction clipped or extended.");
    fixedSize->setIsSecret(true);
    fixedSize->setAnimates(false);
    page->addChild(*fixedSize);
    
    DoubleParamDescriptor* par = desc.defineDoubleParam(kParParamName);
    par->setLabels(kParParamName, kParParamName, kParParamName);
    par->setDefault(1.);
    par->setRange(0.01, INT_MAX);
    par->setHint("The pixel aspect ratio of the output image.");
    par->setIsSecret(true);
    par->setAnimates(false);
    page->addChild(*par);
    
    Double2DParamDescriptor* scale = desc.defineDouble2DParam(kScaleParamName);
    scale->setLabels(kScaleParamName, kScaleParamName, kScaleParamName);
    scale->setDefault(1.,1.);
    scale->setRange(0.01, 0.01, INT_MAX, INT_MAX);
    scale->setHint("The scale factor to apply. If non-uniform you should select a resize type of \"non uniform\".");
    scale->setIsSecret(true);
    page->addChild(*scale);
    
    ChoiceParamDescriptor* resizeType = desc.defineChoiceParam(kResizeTypeParamName);
    resizeType->setLabels(kResizeTypeParamName, kResizeTypeParamName, kResizeTypeParamName);
    resizeType->setAnimates(false);
    resizeType->appendOption("None","Pixels aren't scaled");
    resizeType->appendOption("Width","Scales so it fills the output width");
    resizeType->appendOption("Height","Scales so it fills the output height");
    resizeType->appendOption("Fit","Fit the smaller of width and height");
    resizeType->appendOption("Fill","Fill the smaller of width and height");
    resizeType->appendOption("Non uniform","Non-uniform scaling to fit both width and height");
    resizeType->setDefault(1);
    resizeType->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*resizeType);
    
    BooleanParamDescriptor* flip = desc.defineBooleanParam(kFlipParamName);
    flip->setDefault(false);
    flip->setAnimates(false);
    flip->setLabels(kFlipParamName, kFlipParamName, kFlipParamName);
    flip->setLayoutHint(OFX::eLayoutHintNoNewLine);
    flip->setHint("Upside-down (swaps bottom and top)");
    page->addChild(*flip);
    
    BooleanParamDescriptor* flop = desc.defineBooleanParam(kFlopParamName);
    flop->setLabels(kFlopParamName, kFlopParamName, kFlopParamName);
    flop->setAnimates(false);
    flop->setDefault(false);
    flop->setHint("Mirror image (swaps left and right)");
    page->addChild(*flop);
    
    Double2DParamDescriptor* center = desc.defineDouble2DParam(kCenterParamName);
    center->setLabels(kCenterParamName, kCenterParamName, kCenterParamName);
    center->setDoubleType(OFX::eDoubleTypeXYAbsolute);
    center->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
    center->setDefault(0.5,0.5);
    center->setHint("This is the center point of the scaling.");
    page->addChild(*center);
}


void ReformatPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);
    
    ReformatPluginDescribeInContext(desc, context, page);
    
    Transform3x3DescribeInContextEnd(desc, context, page, false);


}

