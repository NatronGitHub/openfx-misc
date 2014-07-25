/*
 OFX Transform3x3 plugin: a base plugin for 2D homographic transform,
 represented by a 3x3 matrix.

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

#include "ofxsTransform3x3.h"
#include "ofxsTransform3x3Processor.h"
#include "ofxsMerging.h"

using namespace OFX;

// unfortunately, we cannot rely on the host sending changedParam() when the animation changes
// (Nuke doesn't call the action when a linked animation is changed),
// nor on dst->getUniqueIdentifier (which is "ffffffffffffffff" on Nuke)
// DON'T UNCOMMENT //#define USE_CACHE

#define kTransform3x3MotionBlurCount 1000 // number of transforms used in the motion 

static void
shutterRange(double time, double shutter, int shutteroffset, double shuttercustomoffset, OfxRangeD* range)
{
    switch (shutteroffset) {
        case kTransform3x3ShutterOffsetCentered:
            range->min = time - shutter/2;
            range->max = time + shutter/2;
            break;
        case kTransform3x3ShutterOffsetStart:
            range->min = time;
            range->max = time + shutter;
            break;
        case kTransform3x3ShutterOffsetEnd:
            range->min = time - shutter;
            range->max = time;
            break;
        case kTransform3x3ShutterOffsetCustom:
            range->min = time + shuttercustomoffset;
            range->max = time + shuttercustomoffset + shutter;
            break;
        default:
            range->min = time;
            range->max = time;
            break;
    }
}


#ifdef USE_CACHE
class Transform3x3Plugin::CacheID {
public:
    CacheID()
    : _time(0.)
    , _fielded(false)
    , _pixelaspectratio(0.)
    , _invert(false)
    , _shutter(0.)
    , _shutteroffset(0)
    , _shuttercustomoffset(0.)
    , _uniqueIdentifier()
    {
        _renderscale.x = 0.;
        _renderscale.y = 0.;
    }

    CacheID(double time,
            OfxPointD renderscale,
            bool fielded,
            double pixelaspectratio,
            bool invert,
            double shutter,
            int shutteroffset,
            double shuttercustomoffset,
            const std::string& uniqueIdentifier)
    : _time(time)
    , _renderscale(renderscale)
    , _fielded(fielded)
    , _pixelaspectratio(pixelaspectratio)
    , _invert(invert)
    , _shutter(shutter)
    , _shutteroffset(shutteroffset)
    , _shuttercustomoffset(shuttercustomoffset)
    , _uniqueIdentifier(uniqueIdentifier)
    {
    }

    
    bool operator == (const CacheID &b) const
    {
        return (_time == b._time &&
                _renderscale.x == b._renderscale.x &&
                _renderscale.y == b._renderscale.y &&
                _fielded == b._fielded &&
                _pixelaspectratio == b._pixelaspectratio &&
                _invert == b._invert &&
                _shutter == b._shutter &&
                _shutteroffset == b._shutteroffset &&
                _shuttercustomoffset == b._shuttercustomoffset);
    }

    bool operator != (const CacheID &b) const
    {
        return !(*this == b);
    }

    double getTime() const { return _time; }

    OfxPointD getRenderScale() const { return _renderscale; }

    bool getFielded() const { return _fielded; }

    double getPixelAspectRatio() const { return _pixelaspectratio; }

    bool getInvert() const { return _invert; }

    double getShutter() const { return _shutter; }

    int getShutterOffset() const { return _shutteroffset; }

    double getShutterCustomOffset() const { return _shuttercustomoffset; }

    void getShutterRange(OfxRangeD* range) const {
        return shutterRange(_time, _shutter, _shutteroffset, _shuttercustomoffset, range);
    }

private:
    double _time;
    OfxPointD _renderscale;
    bool _fielded;
    double _pixelaspectratio;
    bool _invert;
    double _shutter;
    int _shutteroffset;
    double _shuttercustomoffset;
    std::string _uniqueIdentifier; // unique ID of the source image (see Image::getUniqueIdentifier())
};

// a simple cache for the 1000 transforms used in the motionblur process
template<class ID>
class Transform3x3Plugin::Cache {
public:
    Cache(Transform3x3Plugin* parent)
    : _parent(parent)
    , _valid(false)
    , _id()
    , _invtransform(NULL)
    , _invtransformsize(0)
    , _invtransformsizealloc(0)
    , _mutex(0)
    {
    }

    ~Cache()
    {
        delete _invtransform;
    }

    void purge()
    {
        _valid = false;
    }

    bool get(const ID& id, OFX::Matrix3x3* invtransform, size_t* invtransformsize)
    {
        bool retval = true;
        lock();
        if (!_valid || _id != id) {
            retval = false; // not cached

            if (_invtransform == NULL) {
                assert(_invtransformsizealloc == 0);
                _invtransformsizealloc = kTransform3x3MotionBlurCount;
                _invtransform = new OFX::Matrix3x3[_invtransformsizealloc];
            }
            const bool fielded = id.getFielded();
            const bool invert = id.getInvert();
            const double pixelaspectratio = id.getPixelAspectRatio();
            const OfxPointD renderscale = id.getRenderScale();

            _invtransformsize = _parent->getInverseTransforms(id.getTime(),
                                                              renderscale,
                                                              fielded,
                                                              pixelaspectratio,
                                                              invert,
                                                              id.getShutter(),
                                                              id.getShutterOffset(),
                                                              id.getShutterCustomOffset(),
                                                              _invtransform,
                                                              _invtransformsizealloc);
        }
        _id = id;
        _valid = true;

        memcpy(invtransform, _invtransform, _invtransformsize * sizeof(OFX::Matrix3x3));
        *invtransformsize = _invtransformsize;

        unlock();
        return retval;
    }

    bool getShutterRange(OfxRangeD* range) const {
        lock();
        if (!_valid) {
            unlock();
            return false;
        }
        _id.getShutterRange(range);
        unlock();
        return true;
    }

private:
    void lock() const
    {
        _mutex.lock();
    }

    void unlock() const
    {
        _mutex.unlock();
    }

    Transform3x3Plugin* _parent;
    bool _valid; // easy switch to invalidate the cache
    ID _id;
    OFX::Matrix3x3* _invtransform; // the 1000 transforms
    size_t _invtransformsize;
    size_t _invtransformsizealloc;
    mutable OFX::MultiThread::Mutex _mutex;
};
#endif // USE_CACHE

Transform3x3Plugin::Transform3x3Plugin(OfxImageEffectHandle handle, bool masked)
: ImageEffect(handle)
, dstClip_(0)
, srcClip_(0)
, maskClip_(0)
, _cache(0)
, _invert(0)
, _filter(0)
, _clamp(0)
, _blackOutside(0)
, _motionblur(0)
, _shutter(0)
, _shutteroffset(0)
, _shuttercustomoffset(0)
, _masked(masked)
, _mix(0)
, _maskInvert(0)
{
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA);
    // name of mask clip depends on the context
    if (masked) {
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);
    }
#ifdef USE_CACHE
    _cache = new Cache<CacheID>(this);
#endif
    // Transform3x3-GENERIC
    _invert = fetchBooleanParam(kTransform3x3InvertParamName);
    // GENERIC
    _filter = fetchChoiceParam(kFilterTypeParamName);
    _clamp = fetchBooleanParam(kFilterClampParamName);
    _blackOutside = fetchBooleanParam(kFilterBlackOutsideParamName);
    _motionblur = fetchDoubleParam(kTransform3x3MotionBlurParamName);
    _shutter = fetchDoubleParam(kTransform3x3ShutterParamName);
    _shutteroffset = fetchChoiceParam(kTransform3x3ShutterOffsetParamName);
    _shuttercustomoffset = fetchDoubleParam(kTransform3x3ShutterCustomOffsetParamName);
    if (masked) {
        _mix = fetchDoubleParam(kMixParamName);
        _maskInvert = fetchBooleanParam(kMaskInvertParamName);
    }
}

Transform3x3Plugin::~Transform3x3Plugin()
{
#ifdef USE_CACHE
    delete _cache;
#endif
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
Transform3x3Plugin::setupAndProcess(Transform3x3ProcessorBase &processor, const OFX::RenderArguments &args)
{
    const double time = args.time;
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        dst->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(time));
    size_t invtransformsizealloc = 0;
    size_t invtransformsize = 0;
    OFX::Matrix3x3* invtransform = NULL;

    double motionblur = 0.;
    bool blackOutside = true;
    double mix = 1.;
    bool maskInvert = false;

    if (!src.get()) {
        // no source image, use a dummy transform
        invtransformsizealloc = 1;
        invtransform = new OFX::Matrix3x3[invtransformsizealloc];
        invtransformsize = 1;
        invtransform[0].a = 0.;
        invtransform[0].b = 0.;
        invtransform[0].c = 0.;
        invtransform[0].d = 0.;
        invtransform[0].e = 0.;
        invtransform[0].f = 0.;
        invtransform[0].g = 0.;
        invtransform[0].h = 0.;
        invtransform[0].i = 1.;
    } else {
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatFailed);

        bool invert;
        _invert->getValueAtTime(time, invert);

        _blackOutside->getValueAtTime(time, blackOutside);
        if (_masked) {
            _mix->getValueAtTime(time, mix);
            _maskInvert->getValueAtTime(time, maskInvert);
        }
        _motionblur->getValueAtTime(time, motionblur);
        double shutter;
        _shutter->getValueAtTime(time, shutter);

        const bool fielded = args.fieldToRender == OFX::eFieldLower || args.fieldToRender == OFX::eFieldUpper;
        const double pixelAspectRatio = src->getPixelAspectRatio();

        if (shutter != 0. && motionblur != 0.) {
            invtransformsizealloc = kTransform3x3MotionBlurCount;
            invtransform = new OFX::Matrix3x3[invtransformsizealloc];
            int shutteroffset_i;
            _shutteroffset->getValueAtTime(time, shutteroffset_i);
            double shuttercustomoffset;
            _shuttercustomoffset->getValueAtTime(time, shuttercustomoffset);

#ifdef USE_CACHE
            _cache->get(CacheID(time, args.renderScale, fielded, pixelAspectRatio, invert, shutter, shutteroffset_i, shuttercustomoffset, dst->getUniqueIdentifier()),
                        invtransform, &invtransformsize);
#else
            invtransformsize = getInverseTransforms(time, args.renderScale, fielded, pixelAspectRatio, invert, shutter, shutteroffset_i, shuttercustomoffset, invtransform, invtransformsizealloc);
#endif

        } else {
            invtransformsizealloc = 1;
            invtransform = new OFX::Matrix3x3[invtransformsizealloc];
            invtransformsize = 1;
            bool success = getInverseTransformCanonical(time, invert, &invtransform[0]); // virtual function
            if (!success) {
                invtransform[0].a = 0.;
                invtransform[0].b = 0.;
                invtransform[0].c = 0.;
                invtransform[0].d = 0.;
                invtransform[0].e = 0.;
                invtransform[0].f = 0.;
                invtransform[0].g = 0.;
                invtransform[0].h = 0.;
                invtransform[0].i = 1.;
            } else {
                OFX::Matrix3x3 canonicalToPixel = OFX::ofxsMatCanonicalToPixel(pixelAspectRatio, args.renderScale.x,
                                                                               args.renderScale.y, fielded);
                OFX::Matrix3x3 pixelToCanonical = OFX::ofxsMatPixelToCanonical(pixelAspectRatio,  args.renderScale.x,
                                                                               args.renderScale.y, fielded);
                *invtransform = canonicalToPixel * *invtransform * pixelToCanonical;
            }
        }
        if (invtransformsize == 1) {
            motionblur  = 0.;
        }
    }

    // auto ptr for the mask.
    std::auto_ptr<OFX::Image> mask((_masked && (getContext() != OFX::eContextFilter)) ? maskClip_->fetchImage(time) : 0);

    // do we do masking
    if (_masked && getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get());
    }

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);
    assert(invtransform && invtransformsize);
    processor.setValues(invtransform,
                        invtransformsize,
                        blackOutside,
                        motionblur,
                        mix,
                        maskInvert);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
    delete [] invtransform;
}

// compute the bounding box of the transform of four points
static void
ofxsTransformRegionFromPoints(const OFX::Point3D p[4], OfxRectD &rod)
{
    // extract the x/y bounds
    double x1, y1, x2, y2;

    // if all z's have the same sign, we can compute a reasonable ROI, else we give the whole image (the line at infinity crosses the rectangle)
    bool allpositive = true;
    bool allnegative = true;
    for (int i = 0; i < 4; ++i) {
        allnegative = allnegative && (p[i].z < 0.);
        allpositive = allpositive && (p[i].z > 0.);
    }

    if (!allpositive && !allnegative) {
        // the line at infinity crosses the source RoD
        x1 = kOfxFlagInfiniteMin;
        x2 = kOfxFlagInfiniteMax;
        y1 = kOfxFlagInfiniteMin;
        y2 = kOfxFlagInfiniteMax;
    } else {
        OfxPointD q[4];
        for (int i = 0; i < 4; ++i) {
            q[i].x = p[i].x / p[i].z;
            q[i].y = p[i].y / p[i].z;
        }

        x1 = x2 = q[0].x;
        y1 = y2 = q[0].y;
        for (int i = 1; i < 4; ++i) {
            if (q[i].x < x1) {
                x1 = q[i].x;
            } else if (q[i].x > x2) {
                x2 = q[i].x;
            }
            if (q[i].y < y1) {
                y1 = q[i].y;
            } else if (q[i].y > y2) {
                y2 = q[i].y;
            }
        }
    }

    // GENERIC
    rod.x1 = x1;
    rod.x2 = x2;
    rod.y1 = y1;
    rod.y2 = y2;
    assert(rod.x1 <= rod.x2 && rod.y1 <= rod.y2);
}

// compute the bounding box of the transform of a rectangle
static void
ofxsTransformRegionFromRoD(const OfxRectD &srcRoD, const OFX::Matrix3x3 &transform, OFX::Point3D p[4], OfxRectD &rod)
{
    /// now transform the 4 corners of the source clip to the output image
    p[0] = transform * OFX::Point3D(srcRoD.x1,srcRoD.y1,1);
    p[1] = transform * OFX::Point3D(srcRoD.x1,srcRoD.y2,1);
    p[2] = transform * OFX::Point3D(srcRoD.x2,srcRoD.y2,1);
    p[3] = transform * OFX::Point3D(srcRoD.x2,srcRoD.y1,1);

    ofxsTransformRegionFromPoints(p, rod);
}

void
Transform3x3Plugin::transformRegion(const OfxRectD &rectFrom, double time, bool invert, double motionblur, double shutter, int shutteroffset_i, double shuttercustomoffset, OfxRectD *rectTo)
{
    // Algorithm:
    // - Compute positions of the four corners at start and end of shutter, and every multiple of 0.25 within this range.
    // - Update the bounding box from these positions.
    // - At the end, expand the bounding box by the maximum L-infinity distance between consecutive positions of each corner.

    OfxRangeD range;
    bool hasmotionblur = (shutter != 0. && motionblur != 0.);
    if (hasmotionblur) {
        shutterRange(time, shutter, shutteroffset_i, shuttercustomoffset, &range);
    } else {
        ///if is identity return the input rod instead of transforming
        if (isIdentity(time)) {
            *rectTo = rectFrom;
            return;
        }
        range.min = range.max = time;
    }

    // initialize with a super-empty RoD (note that max and min are reversed)
    rectTo->x1 = kOfxFlagInfiniteMax;
    rectTo->x2 = kOfxFlagInfiniteMin;
    rectTo->y1 = kOfxFlagInfiniteMax;
    rectTo->y2 = kOfxFlagInfiniteMin;
    double t = range.min;
    bool first = true;
    bool last = !hasmotionblur; // ony one iteration if there is no motion blur
    bool finished = false;
    double expand = 0.;
    OFX::Point3D p_prev[4];
    while (!finished) {
        // compute transformed positions
        OfxRectD thisRoD;
        OFX::Matrix3x3 transform;
        bool success = getInverseTransformCanonical(time, invert, &transform); // RoD is computed using the *DIRECT* transform, which is why we use !invert
        if (!success) {
            // return infinite region
            rectTo->x1 = kOfxFlagInfiniteMin;
            rectTo->x2 = kOfxFlagInfiniteMax;
            rectTo->y1 = kOfxFlagInfiniteMin;
            rectTo->y2 = kOfxFlagInfiniteMax;
            return;
        }
        OFX::Point3D p[4];
        ofxsTransformRegionFromRoD(rectFrom, transform, p, thisRoD);

        // update min/max
        MergeImages2D::rectBoundingBox(*rectTo, thisRoD, rectTo);

        // if first iteration, continue
        if (first) {
            first = false;
        } else {
            // compute the L-infinity distance between consecutive tested points
            expand = std::max(expand, std::fabs(p_prev[0].x-p[0].x));
            expand = std::max(expand, std::fabs(p_prev[0].y-p[0].y));
            expand = std::max(expand, std::fabs(p_prev[1].x-p[1].x));
            expand = std::max(expand, std::fabs(p_prev[1].y-p[1].y));
            expand = std::max(expand, std::fabs(p_prev[2].x-p[2].x));
            expand = std::max(expand, std::fabs(p_prev[2].y-p[2].y));
            expand = std::max(expand, std::fabs(p_prev[3].x-p[3].x));
            expand = std::max(expand, std::fabs(p_prev[3].y-p[3].y));
        }

        if (last) {
            finished = true;
        } else {
            // prepare for next iteration
            p_prev[0] = p[0];
            p_prev[1] = p[1];
            p_prev[2] = p[2];
            p_prev[3] = p[3];
            t = std::floor(t*4+1)/4; // next quarter-frame
            if (t >= range.max) {
                // last iteration should be done with range.max
                t = range.max;
                last = true;
            }
        }
    }
    // expand to take into account errors due to motion blur
    if (rectTo->x1 > kOfxFlagInfiniteMin) {
        rectTo->x1 -= expand;
    }
    if (rectTo->x2 < kOfxFlagInfiniteMax) {
        rectTo->x2 += expand;
    }
    if (rectTo->y1 > kOfxFlagInfiniteMin) {
        rectTo->y1 -= expand;
    }
    if (rectTo->y2 < kOfxFlagInfiniteMax) {
        rectTo->y2 += expand;
    }
}

// override the rod call
// Transform3x3-GENERIC
// the RoD should at least contain the region of definition of the source clip,
// which will be filled with black or by continuity.
bool
Transform3x3Plugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    const double time = args.time;
    const OfxRectD srcRoD = srcClip_->getRegionOfDefinition(time);
    if (MergeImages2D::rectIsInfinite(srcRoD)) {
        // return an infinite RoD
        rod.x1 = kOfxFlagInfiniteMin;
        rod.x2 = kOfxFlagInfiniteMax;
        rod.y1 = kOfxFlagInfiniteMin;
        rod.y2 = kOfxFlagInfiniteMax;
        return true;
    }

    double mix = 1.;
    const bool doMasking = _masked && getContext() != OFX::eContextFilter && maskClip_->isConnected();
    if (doMasking) {
        _mix->getValueAtTime(time, mix);
        if (mix == 0.) {
            // identity transform
            rod = srcRoD;
            return true;
        }
    }

    bool invert;
    _invert->getValueAtTime(time, invert);
    invert = !invert; // only for getRoD
    double motionblur;
    _motionblur->getValueAtTime(time, motionblur);
    double shutter;
    _shutter->getValueAtTime(time, shutter);
    int shutteroffset_i;
    _shutteroffset->getValueAtTime(time, shutteroffset_i);
    double shuttercustomoffset;
    _shuttercustomoffset->getValueAtTime(time, shuttercustomoffset);

    // set rod from srcRoD
    transformRegion(srcRoD, time, invert, motionblur, shutter, shutteroffset_i, shuttercustomoffset, &rod);

    bool blackOutside;
    _blackOutside->getValueAtTime(time, blackOutside);

    ofxsFilterExpandRoD(this, dstClip_->getPixelAspectRatio(), args.renderScale, blackOutside, &rod);

    if (doMasking && mix != 1.) {
        // for masking or mixing, we also need the source image.
        // compute the union of both RODs
        MergeImages2D::rectBoundingBox(rod, srcRoD, &rod);
    }
    // say we set it
    return true;
}


// override the roi call
// Transform3x3-GENERIC
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case for transforms)
// It may be difficult to implement for complicated transforms:
// consequently, these transforms cannot support tiles.
void
Transform3x3Plugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    const double time = args.time;
    const OfxRectD roi = args.regionOfInterest;
    OfxRectD srcRoI;

    double mix = 1.;
    const bool doMasking = _masked && getContext() != OFX::eContextFilter && maskClip_->isConnected();
    if (doMasking) {
        _mix->getValueAtTime(time, mix);
        if (mix == 0.) {
            // identity transform
            srcRoI = roi;
            rois.setRegionOfInterest(*srcClip_, srcRoI);
            return;
        }
    }

    bool invert;
    _invert->getValueAtTime(time, invert);
    //invert = !invert; // only for getRoD
    double motionblur;
    _motionblur->getValueAtTime(time, motionblur);
    double shutter;
    _shutter->getValueAtTime(time, shutter);
    int shutteroffset_i;
    _shutteroffset->getValueAtTime(time, shutteroffset_i);
    double shuttercustomoffset;
    _shuttercustomoffset->getValueAtTime(time, shuttercustomoffset);

    // set srcRoI from roi
    transformRegion(roi, time, invert, motionblur, shutter, shutteroffset_i, shuttercustomoffset, &srcRoI);

    int filter;
    _filter->getValueAtTime(time, filter);
    bool blackOutside;
    _blackOutside->getValueAtTime(time, blackOutside);

    assert(srcRoI.x1 <= srcRoI.x2 && srcRoI.y1 <= srcRoI.y2);

    ofxsFilterExpandRoI(roi, srcClip_->getPixelAspectRatio(), args.renderScale, (FilterEnum)filter, doMasking, mix, &srcRoI);

    if (MergeImages2D::rectIsInfinite(srcRoI)) {
        // RoI cannot be infinite.
        // This is not a mathematically correct solution, but better than nothing: set to the project size
        OfxPointD size = getProjectSize();
        OfxPointD offset = getProjectOffset();

        if (srcRoI.x1 <= kOfxFlagInfiniteMin) {
            srcRoI.x1 = offset.x;
        }
        if (srcRoI.x2 >= kOfxFlagInfiniteMax) {
            srcRoI.x2 = offset.x + size.x;
        }
        if (srcRoI.y1 <= kOfxFlagInfiniteMin) {
            srcRoI.y1 = offset.y;
        }
        if (srcRoI.y2 >= kOfxFlagInfiniteMax) {
            srcRoI.y2 = offset.y + size.y;
        }
    }

    if (_masked && mix != 1.) {
        // compute the bounding box with the default ROI
        MergeImages2D::rectBoundingBox(srcRoI, args.regionOfInterest, &srcRoI);
    }

    // no need to set it on mask (the default ROI is OK)
    rois.setRegionOfInterest(*srcClip_, srcRoI);
}


template <class PIX, int nComponents, int maxValue, bool masked>
void
Transform3x3Plugin::renderInternalForBitDepth(const OFX::RenderArguments &args)
{
    const double time = args.time;
    int filter;
    _filter->getValueAtTime(time, filter);
    bool clamp;
    _clamp->getValueAtTime(time, clamp);

    // as you may see below, some filters don't need explicit clamping, since they are
    // "clamped" by construction.
    switch ((FilterEnum)filter) {
        case eFilterImpulse:
        {
            Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterImpulse, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterBilinear:
        {
            Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterBilinear, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterCubic:
        {
            Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterCubic, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterKeys:
            if (clamp) {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterKeys, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterKeys, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterSimon:
            if (clamp) {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterSimon, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterSimon, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterRifman:
            if (clamp) {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterRifman, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterRifman, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterMitchell:
            if (clamp) {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterMitchell, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterMitchell, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterParzen:
        {
            Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterParzen, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterNotch:
        {
            Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterNotch, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
    }
}

// the internal render function
template <int nComponents, bool masked>
void
Transform3x3Plugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch(dstBitDepth)
    {
        case OFX::eBitDepthUByte :
            renderInternalForBitDepth<unsigned char, nComponents, 255, masked>(args);
            break;
        case OFX::eBitDepthUShort :
            renderInternalForBitDepth<unsigned short, nComponents, 65535, masked>(args);
            break;
        case OFX::eBitDepthFloat :
            renderInternalForBitDepth<float, nComponents, 1, masked>(args);
            break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
Transform3x3Plugin::render(const OFX::RenderArguments &args)
{

    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        if (_masked) {
            renderInternal<4,true>(args, dstBitDepth);
        } else {
            renderInternal<4,false>(args, dstBitDepth);
        }
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        if (_masked) {
            renderInternal<3,true>(args, dstBitDepth);
        } else {
            renderInternal<3,false>(args, dstBitDepth);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        if (_masked) {
            renderInternal<1,true>(args, dstBitDepth);
        } else {
            renderInternal<1,false>(args, dstBitDepth);
        }
    }
}

bool Transform3x3Plugin::isIdentity(const RenderArguments &args, OFX::Clip * &identityClip, double &identityTime)
{
    const double time = args.time;

    // if there is motion blur, we suppose the transform is not identity
    double motionblur;
    _motionblur->getValueAtTime(time, motionblur);
    double shutter;
    _shutter->getValueAtTime(time, shutter);
    bool hasmotionblur = (shutter != 0. && motionblur != 0.);
    if (hasmotionblur) {
        return false;
    }

    if (isIdentity(time)) { // let's call the Transform-specific one first
        identityClip = srcClip_;
        identityTime = time;
        return true;
    }

    // GENERIC
    if (_masked) {
        double mix;
        _mix->getValueAtTime(time, mix);
        if (mix == 0.) {
            identityClip = srcClip_;
            identityTime = time;
            return true;
        }
    }
    
    return false;
}

#ifdef OFX_EXTENSIONS_NUKE
// overridden getTransform
bool Transform3x3Plugin::getTransform(const TransformArguments &args, Clip * &transformClip, double transformMatrix[9])
{
    const double time = args.time;
    bool invert;

    //std::cout << "getTransform called!" << std::endl;
    // Transform3x3-GENERIC
    _invert->getValueAtTime(time, invert);

    OFX::Matrix3x3 invtransform;
    bool success = getInverseTransformCanonical(time, invert, &invtransform);
    if (!success) {
        return false;
    }
    double pixelaspectratio = srcClip_->getPixelAspectRatio();
    bool fielded = args.fieldToRender == eFieldLower || args.fieldToRender == eFieldUpper;
    OFX::Matrix3x3 invtransformpixel = (OFX::ofxsMatCanonicalToPixel(pixelaspectratio, args.renderScale.x, args.renderScale.y, fielded) *
                                        invtransform *
                                        OFX::ofxsMatPixelToCanonical(pixelaspectratio, args.renderScale.x, args.renderScale.y, fielded));
    transformClip = srcClip_;
    transformMatrix[0] = invtransformpixel.a;
    transformMatrix[1] = invtransformpixel.b;
    transformMatrix[2] = invtransformpixel.c;
    transformMatrix[3] = invtransformpixel.d;
    transformMatrix[4] = invtransformpixel.e;
    transformMatrix[5] = invtransformpixel.f;
    transformMatrix[6] = invtransformpixel.g;
    transformMatrix[7] = invtransformpixel.h;
    transformMatrix[8] = invtransformpixel.i;
    return true;
}
#endif

size_t
Transform3x3Plugin::getInverseTransforms(double time,
                                         OfxPointD renderscale,
                                         bool fielded,
                                         double pixelaspectratio,
                                         bool invert,
                                         double shutter,
                                         int shutteroffset,
                                         double shuttercustomoffset,
                                         OFX::Matrix3x3* invtransform,
                                         size_t invtransformsizealloc) const
{
    OfxRangeD range;
    shutterRange(time, shutter, shutteroffset, shuttercustomoffset, &range);
    double t_start = range.min;
    double t_end = range.max; // shutter time

    bool allequal = true;
    size_t invtransformsize = invtransformsizealloc;
    OFX::Matrix3x3 canonicalToPixel = OFX::ofxsMatCanonicalToPixel(pixelaspectratio, renderscale.x, renderscale.y, fielded);
    OFX::Matrix3x3 pixelToCanonical = OFX::ofxsMatPixelToCanonical(pixelaspectratio, renderscale.x, renderscale.y, fielded);
    OFX::Matrix3x3 invtransformCanonical;

    for (int i = 0; i < invtransformsize; ++i) {
        double t = (i == 0) ? t_start : (t_start + i*(t_end-t_start)/(double)(invtransformsizealloc-1));
        bool success = getInverseTransformCanonical(t, invert, &invtransformCanonical); // virtual function
        if (success) {
            invtransform[i] = canonicalToPixel * invtransformCanonical * pixelToCanonical;
        } else {
            invtransform[i].a = 0.;
            invtransform[i].b = 0.;
            invtransform[i].c = 0.;
            invtransform[i].d = 0.;
            invtransform[i].e = 0.;
            invtransform[i].f = 0.;
            invtransform[i].g = 0.;
            invtransform[i].h = 0.;
            invtransform[i].i = 1.;
        }
        allequal = allequal && (invtransform[i].a == invtransform[0].a &&
                                invtransform[i].b == invtransform[0].b &&
                                invtransform[i].c == invtransform[0].c &&
                                invtransform[i].d == invtransform[0].d &&
                                invtransform[i].e == invtransform[0].e &&
                                invtransform[i].f == invtransform[0].f &&
                                invtransform[i].g == invtransform[0].g &&
                                invtransform[i].h == invtransform[0].h &&
                                invtransform[i].i == invtransform[0].i);
    }
    if (allequal) { // there is only one transform, no need to do motion blur!
        invtransformsize = 1;
    }
    return invtransformsize;
}

// override changedParam
void
Transform3x3Plugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kTransform3x3InvertParamName ||
        paramName == kTransform3x3ShutterParamName ||
        paramName == kTransform3x3ShutterOffsetParamName ||
        paramName == kTransform3x3ShutterCustomOffsetParamName) {
        // Motion Blur is the only parameter that doesn't matter
        assert(paramName != kTransform3x3MotionBlurParamName);

        changedTransform(args);
    }
}

// override purgeCaches.
void
Transform3x3Plugin::purgeCaches()
{
#ifdef USE_CACHE
    _cache->purge();
    //delete _cache;
    //_cache = new Cache<CacheID>();
#endif
}

// this method must be called by the derived class when the transform was changed
void
Transform3x3Plugin::changedTransform(const OFX::InstanceChangedArgs &args)
{
#ifdef USE_CACHE
    // compute t_start and t_end of the cached transforms.
    // if args.time falls between these, invalidate the transform cache
    OfxRangeD range;
    bool valid = _cache->getShutterRange(&range);
    if (valid && range.min <= args.time && args.time <= range.max) {
        _cache->purge();
    }
#endif
}


void OFX::Transform3x3Describe(OFX::ImageEffectDescriptor &desc, bool masked)
{
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    if (masked) {
        desc.addSupportedContext(eContextPaint);
    }
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    // each field has to be transformed separately, or you will get combing effect
    // this should be true for all geometric transforms
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(eRenderFullySafe);

    // Transform3x3-GENERIC

    // in order to support tiles, the transform plugin must implement the getRegionOfInterest function
    desc.setSupportsTiles(true);

    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(true);

#ifdef OFX_EXTENSIONS_NUKE
    if (!masked) {
        // Enable transform by the host.
        // It is only possible for transforms which can be represented as a 3x3 matrix.
        desc.setCanTransform(true);
        if (getImageEffectHostDescription()->canTransform) {
            //std::cout << "kFnOfxImageEffectCanTransform (describe) =" << desc.getPropertySet().propGetInt(kFnOfxImageEffectCanTransform) << std::endl;
        }
    }
#endif
}

OFX::PageParamDescriptor * OFX::Transform3x3DescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, bool masked)
{
    // GENERIC

    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);

    if (masked && (context == eContextGeneral || context == eContextPaint)) {
        // GENERIC (MASKED)
        //
        // if general or paint context, define the mask clip
        // if paint context, it is a mandated input called 'brush'
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true); // we are a mask input
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(true);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    return page;
}

void OFX::Transform3x3DescribeInContextEnd(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor* page, bool masked)
{

    BooleanParamDescriptor* invert = desc.defineBooleanParam(kTransform3x3InvertParamName);
    invert->setLabels(kTransform3x3InvertParamLabel, kTransform3x3InvertParamLabel, kTransform3x3InvertParamLabel);
    invert->setHint(kTransform3x3InvertParamHint);
    invert->setDefault(false);
    invert->setAnimates(true);
    page->addChild(*invert);

    // GENERIC PARAMETERS
    //

    ofxsFilterDescribeParamsInterpolate2D(desc, page);

    DoubleParamDescriptor* motionblur = desc.defineDoubleParam(kTransform3x3MotionBlurParamName);
    motionblur->setLabels(kTransform3x3MotionBlurParamLabel, kTransform3x3MotionBlurParamLabel, kTransform3x3MotionBlurParamLabel);
    motionblur->setHint(kTransform3x3MotionBlurParamHint);
    motionblur->setDefault(0.);
    motionblur->setRange(0., 100.);
    motionblur->setDisplayRange(0., 4.);
    page->addChild(*motionblur);

    DoubleParamDescriptor* shutter = desc.defineDoubleParam(kTransform3x3ShutterParamName);
    shutter->setLabels(kTransform3x3ShutterParamLabel, kTransform3x3ShutterParamLabel, kTransform3x3ShutterParamLabel);
    shutter->setHint(kTransform3x3ShutterParamHint);
    shutter->setDefault(0.5);
    shutter->setRange(0., 2.);
    shutter->setDisplayRange(0., 2.);
    page->addChild(*shutter);

    ChoiceParamDescriptor* shutteroffset = desc.defineChoiceParam(kTransform3x3ShutterOffsetParamName);
    shutteroffset->setLabels(kTransform3x3ShutterOffsetParamLabel, kTransform3x3ShutterOffsetParamLabel, kTransform3x3ShutterOffsetParamLabel);
    shutteroffset->setHint(kTransform3x3ShutterOffsetParamHint);
    assert(shutteroffset->getNOptions() == kTransform3x3ShutterOffsetCentered);
    shutteroffset->appendOption(kTransform3x3ShutterOffsetCenteredLabel, kTransform3x3ShutterOffsetCenteredHint);
    assert(shutteroffset->getNOptions() == kTransform3x3ShutterOffsetStart);
    shutteroffset->appendOption(kTransform3x3ShutterOffsetStartLabel, kTransform3x3ShutterOffsetStartHint);
    assert(shutteroffset->getNOptions() == kTransform3x3ShutterOffsetEnd);
    shutteroffset->appendOption(kTransform3x3ShutterOffsetEndLabel, kTransform3x3ShutterOffsetEndHint);
    assert(shutteroffset->getNOptions() == kTransform3x3ShutterOffsetCustom);
    shutteroffset->appendOption(kTransform3x3ShutterOffsetCustomLabel, kTransform3x3ShutterOffsetCustomHint);
    shutteroffset->setAnimates(true);
    shutteroffset->setDefault(kTransform3x3ShutterOffsetStart);
    page->addChild(*shutteroffset);

    DoubleParamDescriptor* shuttercustomoffset = desc.defineDoubleParam(kTransform3x3ShutterCustomOffsetParamName);
    shuttercustomoffset->setLabels(kTransform3x3ShutterCustomOffsetParamLabel, kTransform3x3ShutterCustomOffsetParamLabel, kTransform3x3ShutterCustomOffsetParamLabel);
    shuttercustomoffset->setHint(kTransform3x3ShutterCustomOffsetParamHint);
    shuttercustomoffset->setDefault(0.);
    shuttercustomoffset->setRange(-1., 1.);
    shuttercustomoffset->setDisplayRange(-1., 1.);
    page->addChild(*shuttercustomoffset);

    if (masked) {
        // GENERIC (MASKED)
        //
        ofxsMaskMixDescribeParams(desc, page);
#ifdef OFX_EXTENSIONS_NUKE
    } else if (getImageEffectHostDescription()->canTransform) {
        // Transform3x3-GENERIC (NON-MASKED)
        //
        //std::cout << "kFnOfxImageEffectCanTransform in describeincontext(" << context << ")=" << desc.getPropertySet().propGetInt(kFnOfxImageEffectCanTransform) << std::endl;
#endif
    }

}
