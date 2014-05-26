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
 */

#ifndef MISC_TRANSFORMPROCESSOR_H
#define MISC_TRANSFORMPROCESSOR_H

#include "../include/ofxsProcessing.H"
#include "../Misc/ofxsMatrix2D.h"
#include "../Misc/ofxsFilter.h"

class TransformProcessorBase : public OFX::ImageProcessor
{
protected:
    OFX::Image *_srcImg;
    OFX::Image *_maskImg;
    // NON-GENERIC PARAMETERS:
    OFX::Matrix3x3 _invtransform;
    // GENERIC PARAMETERS:
    //FilterEnum _filter;
    //bool _clamp;
    bool _blackOutside;
    bool _domask;
    double _mix;

public:

    TransformProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _invtransform()
    //, _filter(eFilterImpulse)
    //, _clamp(false)
    , _blackOutside(false)
    , _domask(false)
    , _mix(1.0)
    {
    }

    virtual FilterEnum getFilter() const = 0;
    virtual bool getClamp() const = 0;

    /** @brief set the src image */
    void setSrcImg(OFX::Image *v)
    {
        _srcImg = v;
    }


    /** @brief set the optional mask image */
    void setMaskImg(OFX::Image *v) {_maskImg = v;}

    // Are we masking. We can't derive this from the mask image being set as NULL is a valid value for an input image
    void doMasking(bool v) {_domask = v;}

    void setValues(const OFX::Matrix3x3& invtransform, //!< non-generic
                   // all generic parameters below
                   double pixelaspectratio, //!< 1.067 for PAL, where 720x576 pixels occupy 768x576 in canonical coords
                   const OfxPointD& renderscale, //!< 0.5 for a half-resolution image
                   OFX::FieldEnum fieldToRender,
                   //FilterEnum filter,                 //!< generic
                   //bool clamp, //!< generic
                   bool blackOutside, //!< generic
                   double mix)          //!< generic
    {
        bool fielded = fieldToRender == OFX::eFieldLower || fieldToRender == OFX::eFieldUpper;
        // NON-GENERIC
        _invtransform = (OFX::ofxsMatCanonicalToPixel(pixelaspectratio, renderscale.x, renderscale.y, fielded) *
                         invtransform *
                         OFX::ofxsMatPixelToCanonical(pixelaspectratio, renderscale.x, renderscale.y, fielded));
        // GENERIC
        //_filter = filter;
        //_clamp = clamp;
        _blackOutside = blackOutside;
        _mix = mix;
    }
};


// The "masked", "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue, bool masked, FilterEnum filter, bool clamp>
class TransformProcessor : public TransformProcessorBase
{


    public :
    TransformProcessor(OFX::ImageEffect &instance)
    : TransformProcessorBase(instance)
    {
    }

    virtual FilterEnum getFilter() const { return filter; }
    virtual bool getClamp() const { return clamp; }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        float tmpPix[nComponents];

        //assert(filter == _filter);
        for (int y = procWindow.y1; y < procWindow.y2; ++y)
        {
            if(_effect.abort()) break;

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            // the coordinates of the center of the pixel in canonical coordinates
            // see http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#CanonicalCoordinates
            OFX::Point3D canonicalCoords;
            canonicalCoords.z = 1;
            canonicalCoords.y = (double)y + 0.5;

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents)
            {
                // NON-GENERIC TRANSFORM

                // the coordinates of the center of the pixel in canonical coordinates
                // see http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#CanonicalCoordinates
                canonicalCoords.x = (double)x + 0.5;
                OFX::Point3D transformed = _invtransform * canonicalCoords;
                if (!_srcImg || transformed.z == 0.) {
                    // the back-transformed point is at infinity
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = 0;
                    }
                } else {
                    double fx = transformed.z != 0 ? transformed.x / transformed.z : transformed.x;
                    double fy = transformed.z != 0 ? transformed.y / transformed.z : transformed.y;

                    ofxsFilterInterpolate2D<PIX,nComponents,filter,clamp>(fx, fy, _srcImg, _blackOutside, tmpPix);
                }

                ofxsMaskMix<PIX, nComponents, maxValue, masked>(tmpPix, x, y, _srcImg, _domask, _maskImg, _mix, dstPix);
            }
        }
    }
};


#endif // MISC_TRANSFORM_H
