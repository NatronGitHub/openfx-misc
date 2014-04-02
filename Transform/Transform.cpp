/*
 OFX Transform plugin.
 
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

#include "Transform.h"

#include <cmath>
#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif


#include "../include/ofxsProcessing.H"
#include "GenericTransform.h"

#define kTranslateParamName "Translate"
#define kRotateParamName "Rotate"
#define kScaleParamName "Scale"
#define kSkewParamName "Skew"
#define kCenterParamName "Center"
#define kFilterParamName "Filter"
#define kBlackOutsideParamName "Black outside"


#define CIRCLE_RADIUS_BASE 30.0
#define POINT_SIZE 7.0
#define ELLIPSE_N_POINTS 50.0

using namespace OFX;

class TransformProcessorBase : public OFX::ImageProcessor
{
protected:
    OFX::Image *_srcImg;
    // NON-GENERIC PARAMETERS:
    Transform2D::Matrix3x3 _transform;
    // GENERIC PARAMETERS:
    int _filter;
    bool _blackOutside;

public:
    
    TransformProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    {
        
    }
    
    void setSrcImg(OFX::Image *v) {_srcImg = v;}

    void setValues(const OfxPointD& translate, //!< non-generic
                   double rotate,              //!< non-generic
                   const OfxPointD& scale,     //!< non-generic
                   double skew,                //!< non-generic
                   const OfxPointD& center,    //!< non-generic
                   int filter,                 //!< generic
                   bool blackOutside)          //!< generic
    {
        // NON-GENERIC
        _transform = Transform2D::Matrix3x3::getTransform(translate, scale, skew, rotate, center);
        // GENERIC
        _filter = filter;
        _blackOutside = blackOutside;
    }
    
    
};

static void normalize(double *x,double *y,double x1,double x2,double y1,double y2)
{
    *x = (*x - x1) / (x2 - x1);
    *y = (*y - y1) / (y2 - y1);
}

static void denormalize(double* x,double *y,const OfxRectD& rod)
{
    *x = *x * (rod.x2 - rod.x1);
    *y = *y * (rod.y2 - rod.y1);
}

template <class PIX, int nComponents, int maxValue>
class TransformProcessor : public TransformProcessorBase
{
    

public :
    TransformProcessor(OFX::ImageEffect &instance)
    : TransformProcessorBase(instance)
    {
        
    }
    
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        
        for (int y = procWindow.y1; y < procWindow.y2; y++)
        {
            ///convert to a normalized 0 ,1 coordinate
            Transform2D::Point3D normalizedCoords;
            normalizedCoords.z = 1;
            normalizedCoords.y = (double)y; //(y - dstRod.y1) / (double)(dstRod.y2 - dstRod.y1);
            
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            for (int x = procWindow.x1; x < procWindow.x2; x++)
            {
                // NON-GENERIC TRANSFORM

                normalizedCoords.x = (double)x;//(x - dstRod.x1) / (double)(dstRod.x2 - dstRod.x1);
                Transform2D::Point3D transformed = _transform * normalizedCoords;

                if (!_srcImg || transformed.z == 0.) {
                    // the back-transformed point is at infinity
                    for (int c = 0; c < nComponents; ++c) {
                        dstPix[c] = 0;
                    }
                } else {
                    double fx = transformed.x / transformed.z;
                    double fy = transformed.y / transformed.z;

                    // GENERIC TRANSFORM
                    // from here on, everything is generic, and should be moved to a generic transform class
                    OfxRectI bounds = _srcImg->getBounds();
                    if (_filter == 0) {
                        ///nearest neighboor
                        int x = std::floor(fx+0.5);
                        int y = std::floor(fy+0.5);

                        if (!_blackOutside) {
                            x = std::max(bounds.x1,std::min(x,bounds.x2-1));
                            y = std::max(bounds.y1,std::min(y,bounds.y2-1));
                        }
                        PIX *srcPix = (PIX *)_srcImg->getPixelAddress(x, y);
                        if (srcPix) {
                            for (int c = 0; c < nComponents; ++c) {
                                dstPix[c] = srcPix[c];
                            }
                        } else {
                            for (int c = 0; c < nComponents; ++c) {
                                dstPix[c] = 0;
                            }
                        }
                    } else if (_filter == 1) {
                        // bilinear
                        int x = std::floor(fx);
                        int y = std::floor(fy);
                        int nx = x + 1;
                        int ny = y + 1;
                        if (!_blackOutside) {
                            x = std::max(bounds.x1,std::min(x,bounds.x2-1));
                            y = std::max(bounds.y1,std::min(y,bounds.y2-1));
                        }
                        nx = std::max(bounds.x1,std::min(nx,bounds.x2-1));
                        ny = std::max(bounds.y1,std::min(ny,bounds.y2-1));

                        const double dx = std::max(0., std::min(fx - x, 1.));
                        const double dy = std::max(0., std::min(fy - y, 1.));

                        PIX *Pcc = (PIX *)_srcImg->getPixelAddress( x,  y);
                        PIX *Pnc = (PIX *)_srcImg->getPixelAddress(nx,  y);
                        PIX *Pcn = (PIX *)_srcImg->getPixelAddress( x, ny);
                        PIX *Pnn = (PIX *)_srcImg->getPixelAddress(nx, ny);
                        if (Pcc && Pnc && Pcn && Pnn) {
                            for (int c = 0; c < nComponents; ++c) {
                                const double Icc = Pcc[c];
                                const double Inc = Pnc[c];
                                const double Icn = Pcn[c];
                                const double Inn = Pnn[c];

                                dstPix[c] = Icc + dx*(Inc-Icc + dy*(Icc+Inn-Icn-Inc)) + dy*(Icn-Icc);
                            }
                        } else {
                            for (int c = 0; c < nComponents; ++c) {
                                dstPix[c] = 0;
                            }
                        }
                    } else if (_filter == 2) {
                        // bicubic
                        int x = std::floor(fx);
                        int y = std::floor(fy);
                        int px = x - 1;
                        int py = y - 1;
                        int nx = x + 1;
                        int ny = y + 1;
                        int ax = x + 2;
                        int ay = y + 2;
                        if (!_blackOutside) {
                            x = std::max(bounds.x1,std::min(x,bounds.x2-1));
                            y = std::max(bounds.y1,std::min(y,bounds.y2-1));
                        }
                        px = std::max(bounds.x1,std::min(px,bounds.x2-1));
                        py = std::max(bounds.y1,std::min(py,bounds.y2-1));
                        nx = std::max(bounds.x1,std::min(nx,bounds.x2-1));
                        ny = std::max(bounds.y1,std::min(ny,bounds.y2-1));
                        ax = std::max(bounds.x1,std::min(ax,bounds.x2-1));
                        ay = std::max(bounds.y1,std::min(ay,bounds.y2-1));
                        const double dx = std::max(0., std::min(fx - x, 1.));
                        const double dy = std::max(0., std::min(fy - y, 1.));

                        PIX *Ppp = (PIX *)_srcImg->getPixelAddress(px, py);
                        PIX *Pcp = (PIX *)_srcImg->getPixelAddress( x, py);
                        PIX *Pnp = (PIX *)_srcImg->getPixelAddress(nx, py);
                        PIX *Pap = (PIX *)_srcImg->getPixelAddress(nx, py);
                        PIX *Ppc = (PIX *)_srcImg->getPixelAddress(px,  y);
                        PIX *Pcc = (PIX *)_srcImg->getPixelAddress( x,  y);
                        PIX *Pnc = (PIX *)_srcImg->getPixelAddress(nx,  y);
                        PIX *Pac = (PIX *)_srcImg->getPixelAddress(nx,  y);
                        PIX *Ppn = (PIX *)_srcImg->getPixelAddress(px, ny);
                        PIX *Pcn = (PIX *)_srcImg->getPixelAddress( x, ny);
                        PIX *Pnn = (PIX *)_srcImg->getPixelAddress(nx, ny);
                        PIX *Pan = (PIX *)_srcImg->getPixelAddress(nx, ny);
                        PIX *Ppa = (PIX *)_srcImg->getPixelAddress(px, ay);
                        PIX *Pca = (PIX *)_srcImg->getPixelAddress( x, ay);
                        PIX *Pna = (PIX *)_srcImg->getPixelAddress(nx, ay);
                        PIX *Paa = (PIX *)_srcImg->getPixelAddress(nx, ay);
                        if (Ppp && Pcp && Pnp && Pap && Ppc && Pcc && Pnc && Pac && Ppn && Pcn && Pnn && Pan && Ppa && Pca && Pna && Paa) {
                            for (int c = 0; c < nComponents; ++c) {
                                double Ipp = Ppp[c];
                                double Icp = Pcp[c];
                                double Inp = Pnp[c];
                                double Iap = Pap[c];
                                double Ipc = Ppc[c];
                                double Icc = Pcc[c];
                                double Inc = Pnc[c];
                                double Iac = Pac[c];
                                double Ipn = Ppn[c];
                                double Icn = Pcn[c];
                                double Inn = Pnn[c];
                                double Ian = Pan[c];
                                double Ipa = Ppa[c];
                                double Ica = Pca[c];
                                double Ina = Pna[c];
                                double Iaa = Paa[c];
                                double Ip = Icp + 0.5f*(dx*(-Ipp+Inp) + dx*dx*(2*Ipp-5*Icp+4*Inp-Iap) + dx*dx*dx*(-Ipp+3*Icp-3*Inp+Iap));
                                double Ic = Icc + 0.5f*(dx*(-Ipc+Inc) + dx*dx*(2*Ipc-5*Icc+4*Inc-Iac) + dx*dx*dx*(-Ipc+3*Icc-3*Inc+Iac));
                                double In = Icn + 0.5f*(dx*(-Ipn+Inn) + dx*dx*(2*Ipn-5*Icn+4*Inn-Ian) + dx*dx*dx*(-Ipn+3*Icn-3*Inn+Ian));
                                double Ia = Ica + 0.5f*(dx*(-Ipa+Ina) + dx*dx*(2*Ipa-5*Ica+4*Ina-Iaa) + dx*dx*dx*(-Ipa+3*Ica-3*Ina+Iaa));
                                dstPix[c] =  Ic + 0.5f*(dy*(-Ip+In) + dy*dy*(2*Ip-5*Ic+4*In-Ia) + dy*dy*dy*(-Ip+3*Ic-3*In+Ia));
                            }
                        } else {
                            for (int c = 0; c < nComponents; ++c) {
                                dstPix[c] = 0;
                            }
                        }
                    }

                }
                dstPix += nComponents;
                
                
            }
        }
    }
};





////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TransformPlugin : public OFX::ImageEffect
{
protected:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;

public:
    /** @brief ctor */
    TransformPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        // NON-GENERIC
        _translate = fetchDouble2DParam(kTranslateParamName);
        _rotate = fetchDoubleParam(kRotateParamName);
        _scale = fetchDouble2DParam(kScaleParamName);
        _skew = fetchDoubleParam(kSkewParamName);
        _center = fetchDouble2DParam(kCenterParamName);
        // GENERIC
        _filter = fetchChoiceParam(kFilterParamName);
        _blackOutside = fetchBooleanParam(kBlackOutsideParamName);
    }
    
    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod);
    
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);
    
    virtual bool isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime);
    
    /* set up and run a processor */
    void setupAndProcess(TransformProcessorBase &, const OFX::RenderArguments &args);

private:
    // NON-GENERIC
    OFX::Double2DParam* _translate;
    OFX::DoubleParam* _rotate;
    OFX::Double2DParam* _scale;
    OFX::DoubleParam* _skew;
    OFX::Double2DParam* _center;
    // GENERIC
    OFX::ChoiceParam* _filter;
    OFX::BooleanParam* _blackOutside;
};



////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
TransformPlugin::setupAndProcess(TransformProcessorBase &processor, const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
    if (src.get() && dst.get())
    {
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatFailed);

        OfxPointD size = getProjectSize();
        OfxPointD offset = getProjectOffset();
        // NON-GENERIC
        OfxPointD scale;
        OfxPointD translate;
        double rotate;
        double skew;
        OfxPointD center;
        // GENERIC
        int filter;
        bool blackOutside;

        // NON-GENERIC
        _scale->getValue(scale.x, scale.y);
        _translate->getValue(translate.x, translate.y);
        translate.x =  (translate.x * size.x) + offset.x;
        translate.y =  (translate.y * size.y) + offset.y;
        _rotate->getValue(rotate);
        rotate = Transform2D::toRadians(rotate);

        _skew->getValue(skew);
        
        _center->getValue(center.x, center.y);
        center.x =  (center.x * size.x) + offset.x;
        center.y =  (center.y * size.y) + offset.y;
        
        // GENERIC
        _filter->getValue(filter);
        _blackOutside->getValue(blackOutside);

        processor.setValues(translate, rotate, scale, skew, center, filter, blackOutside);
    }

    
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);
    processor.process();
}

// override the rod call
// NON-GENERIC
// the RoD should at least contain the region of definition of the source clip,
// which will be filled with black or by continuity.
bool
TransformPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    OfxRectD srcRoD = srcClip_->getRegionOfDefinition(args.time);
    OfxPointD size = getProjectSize();
    OfxPointD offset = getProjectOffset();
    OfxPointD scale;
    OfxPointD translate;
    double rotate;
    double skew;
    OfxPointD center;

    _scale->getValue(scale.x, scale.y);
    _translate->getValue(translate.x, translate.y);
    translate.x =  (translate.x * size.x) + offset.x;
    translate.y =  (translate.y * size.y) + offset.y;

    _rotate->getValue(rotate);
    
    ///convert to radians
    rotate = rotate * Transform2D::pi / 180.0;
    _center->getValue(center.x, center.y);
    center.x =  (center.x * size.x) + offset.x;
    center.y =  (center.y * size.y) + offset.y;
    _skew->getValue(skew);
    

    Transform2D::Matrix3x3 transform = Transform2D::Matrix3x3::getTransform(translate, scale, skew, rotate, center);
    transform = transform.invert();
    /// now transform the 4 corners of the initial image
    Transform2D::Point3D topLeft = transform * Transform2D::Point3D(srcRoD.x1,srcRoD.y2,1);
    Transform2D::Point3D topRight = transform * Transform2D::Point3D(srcRoD.x2,srcRoD.y2,1);
    Transform2D::Point3D bottomLeft = transform * Transform2D::Point3D(srcRoD.x1,srcRoD.y1,1);
    Transform2D::Point3D bottomRight = transform * Transform2D::Point3D(srcRoD.x2,srcRoD.y1,1);
    
    double l = std::min(std::min(std::min(topLeft.x, bottomLeft.x),topRight.x),bottomRight.x);
    double b = std::min(std::min(std::min(bottomLeft.y, bottomRight.y),topLeft.y),topRight.y);
    double r = std::max(std::max(std::max(topRight.x, bottomRight.x),topLeft.x),bottomLeft.x);
    double t = std::max(std::max(std::max(topLeft.y, topRight.y),bottomLeft.y),bottomRight.y);
    
    //  denormalize(&l, &b, srcRoD);
    // denormalize(&r, &t, srcRoD);

    // No need to round things up here, we must give the *actual* RoD,
    // which should contain the source RoD.
    rod.x1 = std::min(l, srcRoD.x1);
    rod.x2 = std::max(r, srcRoD.x2);
    rod.y1 = std::min(b, srcRoD.y1);
    rod.y2 = std::max(t, srcRoD.y2);

    // say we set it
    return true;
}

// the overridden render function
void
TransformPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA)
    {
        switch(dstBitDepth)
        {
            case OFX::eBitDepthUByte :
            {
                TransformProcessor<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                TransformProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                TransformProcessor<float,4,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
    else
    {
        switch(dstBitDepth)
        {
            case OFX::eBitDepthUByte :
            {
                TransformProcessor<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                TransformProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                TransformProcessor<float,3,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

// overridden is identity
// NON-GENERIC
bool TransformPlugin::isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime)
{
    OfxPointD scale;
    OfxPointD translate;
    double rotate;
    double skew;
    _scale->getValue(scale.x, scale.y);
    _translate->getValue(translate.x, translate.y);
    _rotate->getValue(rotate);
    _skew->getValue(skew);

    if (scale.x == 1. && scale.y == 1. && translate.x == 0 && translate.y == 0 && rotate == 0 && skew == 0) {
        identityClip = srcClip_;
        identityTime = args.time;
        return true;
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////
// stuff for the interact

class TransformInteract : public OFX::OverlayInteract {
    protected :
    enum DrawStateEnum {
        eInActive = 0, //< nothing happening
        eCircleHovered, //< the scale circle is hovered
        eLeftPointHovered, //< the left point of the circle is hovered
        eRightPointHovered, //< the right point of the circle is hovered
        eBottomPointHovered, //< the bottom point of the circle is hovered
        eTopPointHovered, //< the top point of the circle is hovered
        eCenterPointHovered, //< the center point of the circle is hovered
        eRotationBarHovered, //< the rotation bar is hovered
        eShearBarHoverered //< the shear bar is hovered
    };
    
    enum MouseStateEnum {
        eReleased = 0,
        eDraggingCircle,
        eDraggingLeftPoint,
        eDraggingRightPoint,
        eDraggingTopPoint,
        eDraggingBottomPoint,
        eDraggingCenterPoint,
        eDraggingRotationBar,
        eDraggingShearBar
    };
    
    DrawStateEnum _drawState;
    MouseStateEnum _mouseState;
    TransformPlugin* _plugin;
    OfxPointD _lastMousePos;
    
public :
    TransformInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _drawState(eInActive)
    , _mouseState(eReleased)
    , _plugin(dynamic_cast<TransformPlugin*>(effect))
    , _lastMousePos()
    {
        
        assert(_plugin);
        _lastMousePos.x = _lastMousePos.y = 0.;
        addParamToSlaveTo(effect->getParam(kTranslateParamName));
        addParamToSlaveTo(effect->getParam(kRotateParamName));
        addParamToSlaveTo(effect->getParam(kScaleParamName));
        addParamToSlaveTo(effect->getParam(kSkewParamName));
        addParamToSlaveTo(effect->getParam(kCenterParamName));
    
    }
    
    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args);
    virtual bool penMotion(const OFX::PenArgs &args);
    virtual bool penDown(const OFX::PenArgs &args);
    virtual bool penUp(const OFX::PenArgs &args);
    
private:
    
    
    
    void scaleToProject(OfxPointD& p)
    {
        OfxPointD size = _plugin->getProjectSize();
        OfxPointD offset = _plugin->getProjectOffset();
        p.x =  (p.x * size.x) + offset.x;
        p.y = (p.y * size.y) + offset.y;
    }
    
    void unscaleFromProject(OfxPointD& p)
    {
        OfxPointD size = _plugin->getProjectSize();
        OfxPointD offset = _plugin->getProjectOffset();
        p.x = (p.x - offset.x) / size.x;
        p.y = (p.y - offset.y) / size.y;
    }
    
    void unscaleFromProject(double *x , double *y)
    {
        OfxPointD size = _plugin->getProjectSize();
        OfxPointD offset = _plugin->getProjectOffset();
        *x = (*x - offset.x) / size.x;
        *y = (*y - offset.y) / size.y;
    }

    
    OFX::DoubleParam* getShearParam() const
    {
        OFX::DoubleParam* shearParam = dynamic_cast<OFX::DoubleParam*>(_plugin->getParam(kSkewParamName));
        assert(shearParam);
        return shearParam;
    }
    
    
    OFX::DoubleParam* getRotateParam() const
    {
        OFX::DoubleParam* rotateParam = dynamic_cast<OFX::DoubleParam*>(_plugin->getParam(kRotateParamName));
        assert(rotateParam);
        return rotateParam;
    }
    
    
    OFX::Double2DParam* getTranslateParam() const
    {
        OFX::Double2DParam* translateParam = dynamic_cast<OFX::Double2DParam*>(_plugin->getParam(kTranslateParamName));
        assert(translateParam);
        return translateParam;
    }
    
    OFX::Double2DParam* getCenterParam() const
    {
        OFX::Double2DParam* centerParam = dynamic_cast<OFX::Double2DParam*>(_plugin->getParam(kCenterParamName));
        assert(centerParam);
        return centerParam;
    }
    
    void getCenter(OfxPointD& center)
    {
        OFX::Double2DParam* centerParam = getCenterParam();
        OFX::Double2DParam* translationParam = getTranslateParam();
        OfxPointD translate;
        centerParam->getValue(center.x, center.y);
        translationParam->getValue(translate.x, translate.y);
        scaleToProject(translate);
        scaleToProject(center);
        center.x += translate.x;
        center.y += translate.y;
    }
    
    OFX::Double2DParam* getScaleParam() const
    {
        OFX::Double2DParam* scaleParam = dynamic_cast<OFX::Double2DParam*>(_effect->getParam(kScaleParamName));
        assert(scaleParam);
        return scaleParam;
    }
    
    void getScale(OfxPointD& scale)
    {
        OFX::Double2DParam* scaleParam = getScaleParam();
        scaleParam->getValue(scale.x, scale.y);
    }
    
    void getCircleRadius(OfxPointD& radius,const OfxPointD& pixelScale)
    {
        OfxPointD scale;
        getScale(scale);
        radius.x = (scale.x * CIRCLE_RADIUS_BASE * pixelScale.x);
        radius.y = (scale.y * CIRCLE_RADIUS_BASE * pixelScale.y);
    }
    
    void getPoints(OfxPointD& center,OfxPointD& left,OfxPointD& bottom,OfxPointD& top,OfxPointD& right,const OfxPointD& pixelScale)
    {
        getCenter(center);
        OfxPointD radius;
        getCircleRadius(radius,pixelScale);
        left.x = center.x - radius.x ;
        left.y = center.y;
        right.x = center.x + radius.x ;
        right.y = center.y;
        top.x = center.x;
        top.y = center.y + radius.y ;
        bottom.x = center.x;
        bottom.y = center.y - radius.y ;
    }
    
    
    void drawSquare(const OfxPointD& center,bool hovered,const OfxPointD& pixelScale);
    
    void drawEllipse(const OfxPointD& center,const OfxPointD& radius,const OfxPointD& pixelScale,bool hovered);
    
    void drawShearBar(const OfxPointD& penPos,const OfxPointD &center,const OfxPointD& pixelScale,double radiusY,bool hovered);
    
    void drawRotationBar(const OfxPointD& center,const OfxPointD& pixelScale,double radiusX,bool hovered);
    
    static bool squareContains(const Transform2D::Point3D& pos,const OfxRectD& rect,double toleranceX= 0.,double toleranceY = 0.)
    {
        return pos.x >= (rect.x1 - toleranceX) && pos.x < (rect.x2 + toleranceX)
        && pos.y >= (rect.y1 - toleranceY) && pos.y < (rect.y2 + toleranceY);
    }
    
    bool isOnEllipseBorder(const Transform2D::Point3D& pos,const OfxPointD& radius,const OfxPointD& center,double epsilon = 0.1)
    {
        
        double v = (((pos.x - center.x) * (pos.x - center.x)) / (radius.x * radius.x)) +
        (((pos.y - center.y) * (pos.y - center.y)) / (radius.y * radius.y));
        if (v <= (1. + epsilon) && v >= (1. - epsilon)) {
            return true;
        }
        return false;
    }
    
    bool isOnShearBar(const Transform2D::Point3D& pos,double radiusY,const OfxPointD& center,const OfxPointD& pixelScale,double tolerance)
    {
        double barHalfSize = radiusY + (20. * pixelScale.y);
        if (pos.x >= (center.x - tolerance) && pos.x <= (center.x + tolerance) &&
            pos.y >= (center.y - barHalfSize - tolerance) && pos.y <= (center.y + barHalfSize + tolerance)) {
            return true;
        }
        
        return false;
    }
    
    bool isOnRotationBar(const  Transform2D::Point3D& pos,double radiusX,const OfxPointD& center,const OfxPointD& pixelScale,double tolerance)
    {
        double barExtra = 30. * pixelScale.y;
        if (pos.x >= (center.x - tolerance) && pos.x <= (center.x + radiusX + barExtra + tolerance) &&
            pos.y >= (center.y  - tolerance) && pos.y <= (center.y + tolerance)) {
            return true;
        }
        
        return false;
    }
    
    static OfxRectD rectFromCenterPoint(const OfxPointD& center)
    {
        OfxRectD ret;
        ret.x1 = center.x - POINT_SIZE / 2.;
        ret.x2 = center.x + POINT_SIZE / 2.;
        ret.y1 = center.y - POINT_SIZE / 2.;
        ret.y2 = center.y + POINT_SIZE / 2.;
        return ret;
    }
    
    OfxRangeD getViewportSize()
    {
        OfxRangeD ret;
        ret.min = getProperties().propGetDouble(kOfxInteractPropViewportSize, 0);
        ret.max = getProperties().propGetDouble(kOfxInteractPropViewportSize, 1);
        return ret;
    }
    
};

void TransformInteract::drawSquare(const OfxPointD& center,bool hovered,const OfxPointD& pixelScale)
{
    if (hovered) {
        glColor4f(1.0, 0.0, 0.0, 1.0);
    } else {
        glColor4f(1.0, 1.0, 1.0, 1.0);
    }
    double halfWidth = (POINT_SIZE / 2.) * pixelScale.x;
    double halfHeight = (POINT_SIZE / 2.) * pixelScale.y;
    glBegin(GL_POLYGON);
    glVertex2d(center.x - halfWidth, center.y - halfHeight); // bottom left
    glVertex2d(center.x - halfWidth, center.y + halfHeight); // top left
    glVertex2d(center.x + halfWidth, center.y + halfHeight); // bottom right
    glVertex2d(center.x + halfWidth, center.y - halfHeight); // top right
    glEnd();
    
}

void TransformInteract::drawEllipse(const OfxPointD& center,const OfxPointD& radius,const OfxPointD& pixelScale,bool hovered)
{
    double pi_2 = 2. * Transform2D::pi;
    float angle_increment = pi_2 / std::max(radius.x / pixelScale.x,radius.y / pixelScale.y);
    
    
    if (hovered) {
        glColor4f(1.0, 0.0, 0.0, 1.0);
    } else {
        glColor4f(1.0, 1.0, 1.0, 1.0);
    }
    
    glPushMatrix ();
    //  center the oval at x_center, y_center
    glTranslatef (center.x, center.y, 0);
    //  draw the oval using line segments
    glBegin (GL_LINE_LOOP);
    for (double theta = 0.0f; theta < pi_2; theta += angle_increment) {
        glVertex2f (radius.x * std::cos(theta), radius.y * std::sin(theta));
    }
    glEnd ();
    
    glPopMatrix ();
}

void TransformInteract::drawShearBar(const OfxPointD& penPos,const OfxPointD &center,const OfxPointD& pixelScale,double radiusY,bool hovered)
{
    if (hovered) {
        glColor4f(1.0, 0.0, 0.0, 1.0);
    } else {
        glColor4f(1.0, 1.0, 1.0, 1.0);
    }
    
    double barHalfSize = radiusY + (20. * pixelScale.y);
    double arrowYPosition = radiusY + (10. * pixelScale.y);
    double arrowXHalfSize = 10 * pixelScale.x;
    double arrowHeadOffsetX = 3 * pixelScale.x;
    double arrowHeadOffsetY = 3 * pixelScale.y;
    
    glBegin(GL_LINES);
    glVertex2d(center.x, center.y - barHalfSize);
    glVertex2d(center.x, center.y + barHalfSize);
    
    if (hovered) {
        if (penPos.y < center.y) {
            ///draw an arrow in the bottom of the bar
            
            ///draw the central bar
            glVertex2d(center.x - arrowXHalfSize,center.y - arrowYPosition);
            glVertex2d(center.x + arrowXHalfSize, center.y - arrowYPosition);
            
            
            
            ///left triangle
            glVertex2d(center.x - arrowXHalfSize,center.y -  arrowYPosition);
            glVertex2d(center.x - arrowXHalfSize + arrowHeadOffsetX,center.y - arrowYPosition + arrowHeadOffsetY);
            
            glVertex2d(center.x - arrowXHalfSize,center.y - arrowYPosition);
            glVertex2d(center.x - arrowXHalfSize + arrowHeadOffsetX,center.y - arrowYPosition - arrowHeadOffsetY);

            
            ///right triangle
            glVertex2d(center.x + arrowXHalfSize,center.y - arrowYPosition);
            glVertex2d(center.x + arrowXHalfSize - arrowHeadOffsetX,center.y - arrowYPosition + arrowHeadOffsetY);
            
            glVertex2d(center.x + arrowXHalfSize,center.y - arrowYPosition);
            glVertex2d(center.x + arrowXHalfSize - arrowHeadOffsetX,center.y - arrowYPosition - arrowHeadOffsetY);
            
        } else {
            ///draw an arrow in the top of the bar
            ///draw the central bar
            glVertex2d(center.x - arrowXHalfSize,center.y + arrowYPosition);
            glVertex2d(center.x + arrowXHalfSize, center.y + arrowYPosition);
            
            
            
            ///left triangle
            glVertex2d(center.x - arrowXHalfSize,center.y +  arrowYPosition);
            glVertex2d(center.x - arrowXHalfSize + arrowHeadOffsetX,center.y + arrowYPosition + arrowHeadOffsetY);
            
            glVertex2d(center.x - arrowXHalfSize,center.y + arrowYPosition);
            glVertex2d(center.x - arrowXHalfSize + arrowHeadOffsetX,center.y + arrowYPosition - arrowHeadOffsetY);
            
            
            ///right triangle
            glVertex2d(center.x + arrowXHalfSize,center.y + arrowYPosition);
            glVertex2d(center.x + arrowXHalfSize - arrowHeadOffsetX,center.y + arrowYPosition + arrowHeadOffsetY);
            
            glVertex2d(center.x + arrowXHalfSize, center.y +arrowYPosition);
            glVertex2d(center.x + arrowXHalfSize - arrowHeadOffsetX,center.y + arrowYPosition - arrowHeadOffsetY);

        }
    }

    
    glEnd();
    
}

void TransformInteract::drawRotationBar(const OfxPointD& center,const OfxPointD& pixelScale,double radiusX,bool hovered)
{
    if (hovered) {
        glColor4f(1.0, 0.0, 0.0, 1.0);
    } else {
        glColor4f(1.0, 1.0, 1.0, 1.0);
    }
    
    double barExtra = 30. * pixelScale.x;
    glBegin(GL_LINES);
    glVertex2d(center.x, center.y);
    glVertex2d(center.x + radiusX + barExtra, center.y);
    glEnd();
    
    if (hovered) {
        
        double arrowCenterX = center.x + radiusX + barExtra / 2.;
        
        ///draw an arrow slightly bended. This is an arc of circle of radius 5 in X, and 10 in Y.
        OfxPointD arrowRadius;
        arrowRadius.x = 5. * pixelScale.x;
        arrowRadius.y = 10. * pixelScale.y;
        
        float angle_increment = Transform2D::pi / 10.;
        glPushMatrix ();
        //  center the oval at x_center, y_center
        glTranslatef (arrowCenterX, center.y, 0);
        //  draw the oval using line segments
        glBegin (GL_LINE_STRIP);
        for (double theta = - Transform2D::pi / 2.; theta < Transform2D::pi / 2.; theta += angle_increment) {
            glVertex2f (arrowRadius.x * std::cos(theta), arrowRadius.y * std::sin(theta));
        }
        glEnd ();
        
        glPopMatrix ();
        
        double arrowOffsetX = 5. * pixelScale.x;
        double arrowOffsetY = 5. * pixelScale.y;
        
        glBegin(GL_LINES);
        ///draw the top head
        glVertex2f(arrowCenterX, center.y + arrowRadius.y);
        glVertex2f(arrowCenterX, center.y + arrowRadius.y - arrowOffsetY);
        
        glVertex2f(arrowCenterX, center.y + arrowRadius.y);
        glVertex2f(arrowCenterX  + arrowOffsetX, center.y + arrowRadius.y + 1. * pixelScale.y);
        
        ///draw the bottom head
        glVertex2f(arrowCenterX, center.y - arrowRadius.y);
        glVertex2f(arrowCenterX, center.y - arrowRadius.y + arrowOffsetY);
        
        glVertex2f(arrowCenterX, center.y - arrowRadius.y);
        glVertex2f(arrowCenterX  + arrowOffsetX, center.y - arrowRadius.y - 1. * pixelScale.y);

        glEnd();
        

    }
}

// draw the interact
bool TransformInteract::draw(const OFX::DrawArgs &args)
{
    
    OfxPointD center,left,right,bottom,top;
    getPoints(center,left,bottom,top,right,args.pixelScale);
    
    OFX::DoubleParam* rotateParam = getRotateParam();
    double angle;
    rotateParam->getValue(angle);
    
    OFX::DoubleParam* shearParam = getShearParam();
    double shear;
    shearParam->getValue(shear);
    
    glPushMatrix();
    GLdouble shearMatrix[16];
    shearMatrix[0] = 1.; shearMatrix[1] = 0.; shearMatrix[2] = 0.; shearMatrix[3] = 0;
    shearMatrix[4] = shear; shearMatrix[5] = 1.; shearMatrix[6] = 0.; shearMatrix[7] = 0;
    shearMatrix[8] = 0.; shearMatrix[9] = 0.; shearMatrix[10] = 1.; shearMatrix[11] = 0;
    shearMatrix[12] = 0.; shearMatrix[13] = 0.; shearMatrix[14] = 0.; shearMatrix[15] = 1.;
    glMultMatrixd(shearMatrix);
    glTranslated(center.x, center.y, 0.);
    glRotated(angle, 0, 0., 1.);
    glTranslated(-center.x, -center.y, 0.);
    
    OfxPointD radius;
    getCircleRadius(radius, args.pixelScale);
    drawEllipse(center,radius,args.pixelScale, _mouseState == eDraggingCircle || _drawState == eCircleHovered);
    
    drawShearBar(_lastMousePos, center, args.pixelScale, radius.y, _mouseState == eDraggingShearBar || _drawState == eShearBarHoverered);
    
    drawRotationBar(center, args.pixelScale, radius.x, _mouseState == eDraggingRotationBar || _drawState == eRotationBarHovered);
    
    drawSquare(center, _mouseState == eDraggingCenterPoint || _drawState == eCenterPointHovered,args.pixelScale);
    drawSquare(left, _mouseState == eDraggingLeftPoint || _drawState == eLeftPointHovered, args.pixelScale);
    drawSquare(right, _mouseState == eDraggingRightPoint || _drawState == eRightPointHovered, args.pixelScale);
    drawSquare(top, _mouseState == eDraggingTopPoint || _drawState == eTopPointHovered, args.pixelScale);
    drawSquare(bottom, _mouseState == eDraggingBottomPoint || _drawState == eBottomPointHovered, args.pixelScale);
    
    glPopMatrix();
    return true;
}

// overridden functions from OFX::Interact to do things
bool TransformInteract::penMotion(const OFX::PenArgs &args)
{
    OfxPointD center,left,right,top,bottom;
    getPoints(center,left,bottom,top,right,args.pixelScale);
    
    OfxRectD centerPoint = rectFromCenterPoint(center);
    OfxRectD leftPoint = rectFromCenterPoint(left);
    OfxRectD rightPoint = rectFromCenterPoint(right);
    OfxRectD topPoint = rectFromCenterPoint(top);
    OfxRectD bottomPoint = rectFromCenterPoint(bottom);
    
    OfxPointD ellipseRadius;
    getCircleRadius(ellipseRadius, args.pixelScale);
    
    double dx = args.penPosition.x - _lastMousePos.x;
    double dy = args.penPosition.y - _lastMousePos.y;
    
    OFX::DoubleParam* rotateParam = getRotateParam();
    double currentRotation;
    rotateParam->getValue(currentRotation);
    double rot = Transform2D::toRadians(currentRotation);
    
    OFX::DoubleParam* shearParam = getShearParam();
    double shear;
    shearParam->getValue(shear);
    
    Transform2D::Point3D transformedPos;
    transformedPos.x = args.penPosition.x;
    transformedPos.y = args.penPosition.y;
    transformedPos.z = 1.;
    
    Transform2D::Matrix3x3 transform;
    ////for the rotation bar dragging we dont use the same transform, we don't want to undo the rotation transform
    if (_mouseState != eDraggingRotationBar && _mouseState != eDraggingCenterPoint) {
        ///undo shear + rotation to the current position
        transform = Transform2D::Matrix3x3::getTranslate(-center.x, -center.y).invert() *
        Transform2D::Matrix3x3::getRotate(-rot).invert() *
        Transform2D::Matrix3x3::getTranslate(center).invert() *
        Transform2D::Matrix3x3::getShearX(shear).invert();
    } else {
        transform = Transform2D::Matrix3x3::getShearX(shear).invert();
    }
    transformedPos = transform * transformedPos;
    transformedPos.x /= transformedPos.z;
    transformedPos.y /= transformedPos.z;

    
    bool ret = true;
    if (_mouseState == eReleased) {
        double hoverToleranceX = 5 * args.pixelScale.x;
        double hoverToleranceY = 5 * args.pixelScale.y;
        if (squareContains(transformedPos, centerPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eCenterPointHovered;
        } else if (squareContains(transformedPos, leftPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eLeftPointHovered;
        } else if (squareContains(transformedPos, rightPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eRightPointHovered;
        } else if (squareContains(transformedPos, topPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eTopPointHovered;
        } else if (squareContains(transformedPos, bottomPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eBottomPointHovered;
        } else if (isOnEllipseBorder(transformedPos, ellipseRadius, center)) {
            _drawState = eCircleHovered;
        } else if (isOnShearBar(transformedPos,ellipseRadius.y,center,args.pixelScale,hoverToleranceY)) {
            _drawState = eShearBarHoverered;
        } else if (isOnRotationBar(transformedPos, ellipseRadius.x, center, args.pixelScale, hoverToleranceX)) {
            _drawState = eRotationBarHovered;
        } else {
            _drawState = eInActive;
            ret = false;
        }
    } else if (_mouseState == eDraggingCircle) {
        OFX::Double2DParam* scaleParam = getScaleParam();
        
        double minX,minY,maxX,maxY;
        scaleParam->getRange(minX, minY, maxX, maxY);
        
        OfxPointD scale;
        scaleParam->getValue(scale.x, scale.y);
        
        dx /= (CIRCLE_RADIUS_BASE * args.pixelScale.x);
        dy /= (CIRCLE_RADIUS_BASE * args.pixelScale.y);
        
        // y = 1 , x = 0
        int direction = std::abs(dx) < std::abs(dy) ? 1 : 0;
        
        bool isLeftFromCenter = transformedPos.x < center.x;
        bool isBelowFromCenter = transformedPos.y < center.y;
        
        
        ////  | 1 | 2 |
        ////  | 3 | 4 |
        
        int quadrant;
        if (isLeftFromCenter && !isBelowFromCenter) {
            quadrant = 1;
        } else if (!isLeftFromCenter && !isBelowFromCenter) {
            quadrant = 2;
        } else if (!isLeftFromCenter && isBelowFromCenter) {
            quadrant = 4;
        } else {
            quadrant = 3;
        }
        
        if (quadrant == 1) {
            if (direction) {
                scale.y += dy;
                scale.x += dy;
            } else {
                scale.y -= dx;
                scale.x -= dx;
            }
        } else if (quadrant == 2) {
            if (direction) {
                scale.y += dy;
                scale.x += dy;
            } else {
                scale.y += dx;
                scale.x += dx;
            }
        } else if (quadrant == 3) {
            if (direction) {
                scale.y -= dy;
                scale.x -= dy;
            } else {
                scale.y -= dx;
                scale.x -= dx;
            }
        } else if (quadrant == 4) {
            if (direction) {
                scale.y -= dy;
                scale.x -= dy;
            } else {
                scale.y += dx;
                scale.x += dx;
            }
        }
       
        if (scale.y <= minY) {
            scale.y = minY;
        } else if (scale.y >= maxY) {
            scale.y = maxY;
        }
        if (scale.x <= minX) {
            scale.x = minX;
        } else if (scale.x >= maxX) {
            scale.x = maxX;
        }
        
        scaleParam->setValue(scale.x, scale.y);

    } else if (_mouseState == eDraggingLeftPoint) {
        OFX::Double2DParam* scaleParam = getScaleParam();
        
        double minX,minY,maxX,maxY;
        scaleParam->getRange(minX, minY, maxX, maxY);
        
        OfxPointD scale;
        scaleParam->getValue(scale.x, scale.y);
        dx /= (CIRCLE_RADIUS_BASE * args.pixelScale.x);
        scale.x -= dx;
        if (scale.x <= minX) {
            scale.x = minX;
        } else if (scale.x >= maxX) {
            scale.x = maxX;
        }
        
        scaleParam->setValue(scale.x, scale.y);
    } else if (_mouseState == eDraggingRightPoint) {
        OFX::Double2DParam* scaleParam = getScaleParam();
        
        double minX,minY,maxX,maxY;
        scaleParam->getRange(minX, minY, maxX, maxY);
        
        OfxPointD scale;
        scaleParam->getValue(scale.x, scale.y);
        dx /= (CIRCLE_RADIUS_BASE * args.pixelScale.x);
        scale.x += dx;
        if (scale.x >= maxX) {
            scale.x = maxX;
        } else if (scale.x <= minX) {
            scale.x = minX;
        }
        scaleParam->setValue(scale.x, scale.y);
    } else if (_mouseState == eDraggingTopPoint) {
        OFX::Double2DParam* scaleParam = getScaleParam();
        
        double minX,minY,maxX,maxY;
        scaleParam->getRange(minX, minY, maxX, maxY);
        
        OfxPointD scale;
        scaleParam->getValue(scale.x, scale.y);
        dy /= (CIRCLE_RADIUS_BASE * args.pixelScale.y);
        scale.y += dy;
        if (scale.y >= maxY) {
            scale.y = maxY;
        } else if (scale.y <= minY) {
            scale.y = minY;
        }
        scaleParam->setValue(scale.x, scale.y);
    } else if (_mouseState == eDraggingBottomPoint) {
        OFX::Double2DParam* scaleParam = getScaleParam();
        
        double minX,minY,maxX,maxY;
        scaleParam->getRange(minX, minY, maxX, maxY);
        
        OfxPointD scale;
        scaleParam->getValue(scale.x, scale.y);
        dy /= (CIRCLE_RADIUS_BASE * args.pixelScale.y);
        scale.y -= dy;
        if (scale.y <= minY) {
            scale.y = minY;
        } else if (scale.y >= maxY) {
            scale.y = maxY;
        }

        scaleParam->setValue(scale.x, scale.y);
    } else if (_mouseState == eDraggingCenterPoint) {
        OFX::Double2DParam* translateParam = getTranslateParam();
        OfxPointD currentTranslation;
        translateParam->getValue(currentTranslation.x, currentTranslation.y);
        
        Transform2D::Point3D lastPosTransformed;
        lastPosTransformed.x = _lastMousePos.x;
        lastPosTransformed.y = _lastMousePos.y;
        lastPosTransformed.z = 1.;
        
        lastPosTransformed = transform * lastPosTransformed;
        lastPosTransformed.x /= lastPosTransformed.z;
        lastPosTransformed.y /= lastPosTransformed.z;
        
        dx = transformedPos.x - lastPosTransformed.x;
        dy = transformedPos.y - lastPosTransformed.y;
        unscaleFromProject(&dx,&dy);
        currentTranslation.x += dx;
        currentTranslation.y += dy;
        translateParam->setValue(currentTranslation.x,currentTranslation.y);
    } else if (_mouseState == eDraggingRotationBar) {
        OfxPointD diffToCenter;
        ///the current mouse position (untransformed) is doing has a certain angle relative to the X axis
        ///which can be computed by : angle = arctan(opposite / adjacent)
        diffToCenter.y = transformedPos.y - center.y;
        diffToCenter.x = transformedPos.x - center.x;
        double angle = std::atan2(diffToCenter.y, diffToCenter.x);
        rotateParam->setValue(Transform2D::toDegrees(angle));
        
    } else if (_mouseState == eDraggingShearBar) {
        OfxPointD delta;
        delta.x = dx;
        delta.y = dy;
        unscaleFromProject(delta);
        shearParam->setValue(shear + delta.x);
    } else {
        assert(false);
    }
    _lastMousePos = args.penPosition;
    _effect->redrawOverlays();
    return ret;
    
}

bool TransformInteract::penDown(const OFX::PenArgs &args)
{
    OfxPointD center,left,right,top,bottom;
    getPoints(center,left,bottom,top,right,args.pixelScale);
    OfxRectD centerPoint = rectFromCenterPoint(center);
    OfxRectD leftPoint = rectFromCenterPoint(left);
    OfxRectD rightPoint = rectFromCenterPoint(right);
    OfxRectD topPoint = rectFromCenterPoint(top);
    OfxRectD bottomPoint = rectFromCenterPoint(bottom);
    
    OfxPointD ellipseRadius;
    getCircleRadius(ellipseRadius, args.pixelScale);
    
    
    OFX::DoubleParam* rotateParam = getRotateParam();
    double currentRotation;
    rotateParam->getValue(currentRotation);
    
    OFX::DoubleParam* shearParam = getShearParam();
    double shear;
    shearParam->getValue(shear);
    
    Transform2D::Point3D transformedPos;
    transformedPos.x = args.penPosition.x;
    transformedPos.y = args.penPosition.y;
    transformedPos.z = 1.;
    
    double rot = Transform2D::toRadians(currentRotation);
    
    ///now undo shear + rotation to the current position
    Transform2D::Matrix3x3 transform =
    Transform2D::Matrix3x3::getTranslate(-center.x, -center.y).invert() *
    Transform2D::Matrix3x3::getRotate(-rot).invert() *
    Transform2D::Matrix3x3::getTranslate(center).invert() *
    Transform2D::Matrix3x3::getShearX(shear).invert();
    transformedPos = transform * transformedPos;
    transformedPos.x /= transformedPos.z;
    transformedPos.y /= transformedPos.z;


    
    double pressToleranceX = 5 * args.pixelScale.x;
    double pressToleranceY = 5 * args.pixelScale.y;
    bool ret = true;
    if (squareContains(transformedPos, centerPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingCenterPoint;
    } else if (squareContains(transformedPos, leftPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingLeftPoint;
    } else if (squareContains(transformedPos, rightPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingRightPoint;
    } else if (squareContains(transformedPos, topPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingTopPoint;
    } else if (squareContains(transformedPos, bottomPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingBottomPoint;
    } else if (isOnEllipseBorder(transformedPos,ellipseRadius, center)) {
        _mouseState = eDraggingCircle;
    } else if (isOnShearBar(transformedPos,ellipseRadius.y,center,args.pixelScale,pressToleranceY)) {
        _mouseState = eDraggingShearBar;
    } else if (isOnRotationBar(transformedPos, ellipseRadius.x, center, args.pixelScale, pressToleranceY)) {
        _mouseState = eDraggingRotationBar;
    } else {
        _mouseState = eReleased;
        ret =  false;
    }
    
    _lastMousePos = args.penPosition;
    
    _effect->redrawOverlays();
    return ret;
}

bool TransformInteract::penUp(const OFX::PenArgs &args)
{
    bool ret = _mouseState != eReleased;
    _mouseState = eReleased;
    _lastMousePos = args.penPosition;
    _effect->redrawOverlays();
    return ret;
}


using namespace OFX;

class TransformOverlayDescriptor : public DefaultEffectOverlayDescriptor<TransformOverlayDescriptor, TransformInteract> {};

void TransformPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("TransformOFX", "TransformOFX", "TransformOFX");
    desc.setPluginGrouping("Transform");
    desc.setPluginDescription("Translate / Rotate / Scale a 2D image.");
    
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);
    
    // set a few flags

    // GENERIC

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    // each field has to be transformed separately, or you will get combing effect
    // this should be true for all geometric transforms
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(eRenderFullySafe);

    // NON-GENERIC

    // in order to support tiles, the plugin must implement the getRegionOfInterest function
    // TODO: easy
    desc.setSupportsTiles(false);

    // in order to support multiresolution, the plugin must take into account the renderscale
    // and scale the transform appropriately
    // TODO: could be done easily.
    desc.setSupportsMultiResolution(false);
    desc.setOverlayInteractDescriptor( new TransformOverlayDescriptor);

}


void TransformPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    if (!getImageEffectHostDescription()->supportsParametricParameter) {
        throwHostMissingSuiteException(kOfxParametricParameterSuite);
    }

    // GENERIC

    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(true);
    
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // NON-GENERIC PARAMETERS
    //
    Double2DParamDescriptor* translate = desc.defineDouble2DParam(kTranslateParamName);
    translate->setLabels(kTranslateParamName, kTranslateParamName, kTranslateParamName);
    translate->setDoubleType(eDoubleTypeNormalisedXY);
    translate->setDefault(0, 0);
    page->addChild(*translate);
    
    DoubleParamDescriptor* rotate = desc.defineDoubleParam(kRotateParamName);
    rotate->setLabels(kRotateParamName, kRotateParamName, kRotateParamName);
    rotate->setDefault(0);
    rotate->setRange(-180, 180);
    rotate->setDisplayRange(-180, 180);
    page->addChild(*rotate);
    
    Double2DParamDescriptor* scale = desc.defineDouble2DParam(kScaleParamName);
    scale->setLabels(kScaleParamName, kScaleParamName, kScaleParamName);
    scale->setDefault(1,1);
    scale->setRange(0.1,0.1,10,10);
    scale->setDisplayRange(0.1, 0.1, 10, 10);
    page->addChild(*scale);
    
    DoubleParamDescriptor* skew = desc.defineDoubleParam(kSkewParamName);
    skew->setLabels(kSkewParamName, kSkewParamName, kSkewParamName);
    skew->setDefault(0);
    skew->setRange(-100, 100);
    skew->setDisplayRange(-1,1);
    page->addChild(*skew);
    
    Double2DParamDescriptor* center = desc.defineDouble2DParam(kCenterParamName);
    center->setLabels(kCenterParamName, kCenterParamName, kCenterParamName);
    center->setDoubleType(eDoubleTypeNormalisedXY);
    center->setDefault(0.5, 0.5);
    page->addChild(*center);


    // GENERIC PARAMETERS
    //
    ChoiceParamDescriptor* filter = desc.defineChoiceParam(kFilterParamName);
    filter->setLabels(kFilterParamName, kFilterParamName, kFilterParamName);
    filter->setDefault(2);
    filter->appendOption("Nearest neighboor");
    filter->appendOption("Bilinear");
    filter->appendOption("Bicubic");
    filter->setAnimates(false);
    page->addChild(*filter);

    BooleanParamDescriptor* blackOutside = desc.defineBooleanParam(kBlackOutsideParamName);
    blackOutside->setLabels(kBlackOutsideParamName, kBlackOutsideParamName, kBlackOutsideParamName);
    blackOutside->setDefault(false);
    blackOutside->setAnimates(false);
    page->addChild(*blackOutside);
}

OFX::ImageEffect* TransformPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new TransformPlugin(handle);
}
