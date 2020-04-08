/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2018 INRIA
 *
 * openfx-misc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

//
//  CImgFilter.h
//
//  A base class to simplify the creation of CImg plugins that have one image as input and an optional mask.
//

#ifndef Misc_CImgFilter_h
#define Misc_CImgFilter_h

#define PLUGIN_PACK_GPL2 // include GPL2 plugins by default

#include <cassert>
#include <memory>
#include <algorithm> // max

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxsPixelProcessor.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#include "ofxsThreadSuite.h"

#ifdef thread_local
# define HAVE_THREAD_LOCAL
#else
# if __STDC_VERSION__ >= 201112 && !defined __STDC_NO_THREADS__
#  define thread_local _Thread_local
#  define HAVE_THREAD_LOCAL
# elif defined _WIN32 && ( \
    defined _MSC_VER || \
    defined __ICL || \
    defined __DMC__ || \
    defined __BORLANDC__ )
// "For DLLs that are loaded dynamically after the process has started (delay load, COM objects,
// explicit LoadLibrary, etc) __declspec(thread) does not work on Windows XP, 2003 Server and
// earlier OSes, but does work on Vista and 2008 Server."
// https://web.archive.org/web/20121022172711/http://msdn.microsoft.com:80/en-US/library/9w1sdazb(v=vs.80).aspx#1
// Unfortunately, OFX plugins are loaded using LoadLibrary()
// _WIN32_WINNT = 0x0600 for Windows Server 2008 and Windows Vista.
#  if defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0600)
#    define thread_local __declspec(thread)
#    define HAVE_THREAD_LOCAL
#  else
#    pragma message WARN("CImg plugins cannot be aborted when compiled with this compiler. Please use MinGW, GCC or Clang.")
#  endif
/* note that ICC (linux) and Clang are covered by __GNUC__ */
# elif defined __GNUC__ || \
    defined __SUNPRO_C || \
    defined __xlC__
// Clang 3.4 also support SD-6 (feature test macros __cpp_*), but no thread local macro
#   if defined(__clang__)
#    if __has_feature(cxx_thread_local)
// thread_local was added in Clang 3.3
// Still requires libstdc++ from GCC 4.8
// For that __GLIBCXX__ isn't good enough
// Also the MacOS build of clang does *not* support thread local yet.
#      define thread_local __thread
#      define HAVE_THREAD_LOCAL
#    elif __has_feature(c_thread_local) || __has_extension(c_thread_local)
#      define thread_local _Thread_local
#      define HAVE_THREAD_LOCAL
#    endif

#   elif defined(__GNUC__)
//#    if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 8)
//// The C++11 thread_local keyword is supported in GCC only since 4.8
//#      define thread_local __thread
//#    endif
//#    define HAVE_THREAD_LOCAL
// __thread became widely supported with GCC 4.7
#    if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)
#      define thread_local __thread
#      define HAVE_THREAD_LOCAL
#    endif
#   endif
# endif
#endif

#if !defined(HAVE_THREAD_LOCAL)
#ifdef WIN32
#pragma message WARN("Most CImg plugins cannot be aborted when compiled with this compiler. Please use MinGW, GCC or Clang.")
#else
// non-Win32 systems should have at least pthread
#pragma message WARN("CImg plugins use pthread-based thread-local storage.")
#include <assert.h>
#include <pthread.h>
#define HAVE_PTHREAD
#endif
#endif //!HAVE_THREAD_LOCAL


//#define CIMG_DEBUG

// use the locally-downloaded CImg.h
//
// To download the latest CImg.h, use:
// git archive --remote=git://git.code.sf.net/p/gmic/source HEAD:src CImg.h |tar xf -
//
// CImg.h must at least be the version from Oct 17 2014, commit 9b52016cab3368744ea9f3cc20a3e9b4f0c66eb3
// To download, use:
// git archive --remote=git://git.code.sf.net/p/gmic/source 9b52016cab3368744ea9f3cc20a3e9b4f0c66eb3:src CImg.h |tar xf -
#define cimg_display 0
#define cimg_namespace_suffix openfx_misc
#ifdef _OPENMP
#  ifndef cimg_use_openmp
#    define cimg_use_openmp 1
#  endif
#else
#  ifndef cimg_use_openmp
#    define cimg_use_openmp 0
#  endif
#endif
#define cimg_verbosity 0

// Abort mechanism:
// we have a struct with a thread-local storage that holds the OFX::ImageEffect
// for the thread being rendered
#if defined(HAVE_THREAD_LOCAL) || defined(HAVE_PTHREAD)
#define cimg_abort_test gImageEffectAbort()
inline void gImageEffectAbort();
#endif


#ifdef cimg_version
#error "CImg.h was included before this file"
#endif

#ifdef PLUGIN_PACK_GPL2

// include the inpaint and nlmeans cimg plugins
#if 0 // not necessary since CImg cimmit 7c83bdad65ab7447220b54851a5a1035976777fa
namespace cimg_library_openfx_misc {
    namespace cimg {
        //! Return the maximum between two values.
        template<typename t>
        inline t max(const t& a, const t& b) {
            return (std::max)(a,b);
        }
        //! Return the minimum between two values.
        template<typename t>
        inline t min(const t& a, const t& b) {
            return (std::min)(a,b);
        }
    }
}
#endif
#define cimg_plugin "Inpaint/inpaint.h"
//#define cimg_plugin1 "nlmeans.h"
#endif

CLANG_DIAG_OFF(shorten-64-to-32)
#include "CImg.h"
CLANG_DIAG_ON(shorten-64-to-32)
#define cimg_library cimg_library_suffixed // so that namespace is private, but code requires no change

typedef float cimgpix_t;
typedef float cimgpixfloat_t;

#define CIMG_ABORTABLE // use abortable versions of CImg functions

#if defined(HAVE_THREAD_LOCAL)
struct tls
{
    static thread_local OFX::ImageEffect *gImageEffect;
};

inline void
gImageEffectAbort()
{
#  if cimg_use_openmp!=0
    if ( omp_get_thread_num() ) {
        return;
    }
#  endif
    if ( tls::gImageEffect && tls::gImageEffect->abort() ) {
        throw cimg_library::CImgAbortException("");
    }
}

#elif defined(HAVE_PTHREAD)
struct tls
{
    // the key is created once and *never* deleted using pthread_key_delete()
    static pthread_key_t gImageEffect_key;
    static void gImageEffect_key_delete(void * arg)
    {
        assert (NULL != arg);
        free(arg);
    }
    static void gImageEffect_key_create(void)
    {
        int _ret;
        _ret = pthread_key_create(&(gImageEffect_key), gImageEffect_key_delete);
        cimg_library::cimg::unused(_ret); /* To get rid of warnings in case of NDEBUG */
        assert (0 == _ret);
    }
    static pthread_once_t gImageEffect_once/* = PTHREAD_ONCE_INIT*/;
};

inline void
gImageEffectAbort()
{
#  if cimg_use_openmp!=0
    if ( omp_get_thread_num() ) {
        return;
    }
#  endif
    OFX::ImageEffect **_ptr = (OFX::ImageEffect **)pthread_getspecific(tls::gImageEffect_key);
    if ( *_ptr && (*_ptr)->abort() ) {
        throw cimg_library::CImgAbortException("");
    }
}
#endif


class CImgFilterPluginHelperBase
    : public OFX::ImageEffect
{
public:

    CImgFilterPluginHelperBase(OfxImageEffectHandle handle,
                               bool usesMask, // true if the mask parameter to render should be a single-channel image containing the mask
                               bool supportsComponentRemapping, // true if the number and order of components of the image passed to render() has no importance
                               bool supportsTiles,
                               bool supportsMultiResolution,
                               bool supportsRenderScale,
                               bool defaultUnpremult /* = true*/,
                               bool isFilter /* = true*/);


    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE;
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE;
    static OFX::PageParamDescriptor* describeInContextBegin(bool sourceIsOptional,
                                                            OFX::ImageEffectDescriptor &desc,
                                                            OFX::ContextEnum context,
                                                            bool supportsRGBA,
                                                            bool supportsRGB,
                                                            bool supportsXY,
                                                            bool supportsAlpha,
                                                            bool supportsTiles,
                                                            bool processRGB /* = true*/,
                                                            bool processAlpha /* = false*/,
                                                            bool processIsSecret /* = false*/);
    static void describeInContextEnd(OFX::ImageEffectDescriptor &desc,
                                     OFX::ContextEnum context,
                                     OFX::PageParamDescriptor* page,
                                     bool hasUnpremult = true);

protected:
#ifdef CIMG_DEBUG
    static void printRectI(const char*name,
                           const OfxRectI& rect)
    {
        printf("%s= (%d, %d)-(%d, %d)\n", name, rect.x1, rect.y1, rect.x2, rect.y2);
    }

    static void printRectD(const char*name,
                           const OfxRectD& rect)
    {
        printf("%s= (%g, %g)-(%g, %g)\n", name, rect.x1, rect.y1, rect.x2, rect.y2);
    }

#else
    static void printRectI(const char*,
                           const OfxRectI&) {}

    static void printRectD(const char*,
                           const OfxRectD&) {}

#endif


    void setupAndFill(OFX::PixelProcessorFilterBase & processor,
                      const OfxRectI &renderWindow,
                      const OfxPointD &renderScale,
                      void *dstPixelData,
                      const OfxRectI& dstBounds,
                      OFX::PixelComponentEnum dstPixelComponents,
                      int dstPixelComponentCount,
                      OFX::BitDepthEnum dstPixelDepth,
                      int dstRowBytes);

    void setupAndCopy(OFX::PixelProcessorFilterBase & processor,
                      double time,
                      const OfxRectI &renderWindow,
                      const OfxPointD &renderScale,
                      const OFX::Image* orig,
                      const OFX::Image* mask,
                      const void *srcPixelData,
                      const OfxRectI& srcBounds,
                      OFX::PixelComponentEnum srcPixelComponents,
                      int srcPixelComponentCount,
                      OFX::BitDepthEnum srcBitDepth,
                      int srcRowBytes,
                      int srcBoundary,
                      void *dstPixelData,
                      const OfxRectI& dstBounds,
                      OFX::PixelComponentEnum dstPixelComponents,
                      int dstPixelComponentCount,
                      OFX::BitDepthEnum dstPixelDepth,
                      int dstRowBytes,
                      bool premult,
                      int premultChannel,
                      double mix,
                      bool maskInvert);


    // utility functions
    static
    bool maskLineIsZero(const OFX::Image* mask, int x1, int x2, int y, bool maskInvert);
    static
    bool maskColumnIsZero(const OFX::Image* mask, int x, int y1, int y2, bool maskInvert);

protected:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;

    // params
    OFX::BooleanParam* _processR;
    OFX::BooleanParam* _processG;
    OFX::BooleanParam* _processB;
    OFX::BooleanParam* _processA;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskApply;
    OFX::BooleanParam* _maskInvert;
    bool _usesMask; // true if the mask parameter to render() should be a single-channel mask of the same size as the image
    bool _supportsComponentRemapping; // true if the number and order of components of the image passed to render() has no importance
    bool _supportsTiles;
    bool _supportsMultiResolution;
    bool _supportsRenderScale;
    bool _defaultUnpremult; //!< unpremult by default
    OFX::BooleanParam* _premultChanged; // set to true the when user changes premult
};

template <class Params, bool sourceIsOptional>
class CImgFilterPluginHelper
    : public CImgFilterPluginHelperBase
{
public:

    CImgFilterPluginHelper(OfxImageEffectHandle handle,
                           bool usesMask, // true if the mask parameter to render should be a single-channel image containing the mask
                           bool supportsComponentRemapping, // true if the number and order of components of the image passed to render() has no importance
                           bool supportsTiles,
                           bool supportsMultiResolution,
                           bool supportsRenderScale,
                           bool defaultUnpremult /* = true*/)
        : CImgFilterPluginHelperBase(handle, usesMask, supportsComponentRemapping, supportsTiles, supportsMultiResolution, supportsRenderScale, defaultUnpremult, /*isFilter=*/ true)
    {
    }

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip* &identityClip, double &identityTime, int& /*view*/, std::string& /*plane*/) OVERRIDE FINAL;

    // the following functions can be overridden/implemented by the plugin
    virtual void getValuesAtTime(double time, Params& params) = 0;

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& renderScale, const Params& params, OfxRectI* roi) = 0;
    virtual bool getRegionOfDefinition(const OfxRectI& /*srcRoD*/,
                                       const OfxPointD& /*renderScale*/,
                                       const Params& /*params*/,
                                       OfxRectI* /*dstRoD*/) { return false; };
    virtual void render(const OFX::RenderArguments &args,
                        const Params& params,
                        int x1, //!< origin of the image tile
                        int y1, //!< origin of the image tile
                        cimg_library::CImg<cimgpix_t>& mask, //!< in: if the filter uses the mask, a single-channel mask (can be modified by the render func without any side-effect), else an empty image.
                        cimg_library::CImg<cimgpix_t>& cimg, //!< in/out: image
                        int alphaChannel //!< alpha channel in cimg, or -1 if there is no alpha channel
                        ) = 0;
    virtual bool isIdentity(const OFX::IsIdentityArguments & /*args*/,
                            const Params& /*params*/) { return false; };

    // 0: Black/Dirichlet, 1: Nearest/Neumann, 2: Repeat/Periodic
    virtual int getBoundary(const Params& /*params*/) { return 0; }

    //static void describe(OFX::ImageEffectDescriptor &desc, bool supportsTiles);

    static OFX::PageParamDescriptor* describeInContextBegin(OFX::ImageEffectDescriptor &desc,
                                                            OFX::ContextEnum context,
                                                            bool supportsRGBA,
                                                            bool supportsRGB,
                                                            bool supportsXY,
                                                            bool supportsAlpha,
                                                            bool supportsTiles,
                                                            bool processRGB /* = true*/,
                                                            bool processAlpha /* = false*/,
                                                            bool processIsSecret /* = false*/)
    {
        return CImgFilterPluginHelperBase::describeInContextBegin(sourceIsOptional,
                                                                  desc,
                                                                  context,
                                                                  supportsRGBA,
                                                                  supportsRGB,
                                                                  supportsXY,
                                                                  supportsAlpha,
                                                                  supportsTiles,
                                                                  processRGB,
                                                                  processAlpha,
                                                                  processIsSecret);
    }
};


template <class Params, bool sourceIsOptional>
void
CImgFilterPluginHelper<Params, sourceIsOptional>::render(const OFX::RenderArguments &args)
{
# ifndef NDEBUG
    if ( !_supportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
# endif

    const double time = args.time;
    const OfxPointD& renderScale = args.renderScale;
    const OfxRectI& renderWindow = args.renderWindow;
    OFX::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(time) );
    if ( !dst.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
    const OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    const OFX::PixelComponentEnum dstPixelComponents  = dst->getPixelComponents();
    const int dstPixelComponentCount = dst->getPixelComponentCount();
    assert(dstBitDepth == OFX::eBitDepthFloat); // only float is supported for now (others are untested)

    OFX::auto_ptr<const OFX::Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                         _srcClip->fetchImage(args.time) : 0 );
    if ( src.get() ) {
#     ifndef NDEBUG
        OFX::BitDepthEnum srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcPixelComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcPixelComponents != dstPixelComponents) ) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        checkBadRenderScaleOrField(src, args);
#     endif
#if 0
    } else {
        // src is considered black and transparent, just fill black to dst and return
#pragma message WARN("BUG: even when src is black and transparent, the output may not be black (cf. NoiseCImg)")
        void* dstPixelData = NULL;
        OfxRectI dstBounds;
        OFX::PixelComponentEnum dstPixelComponents;
        OFX::BitDepthEnum dstBitDepth;
        int dstRowBytes;
        getImageData(dst.get(), &dstPixelData, &dstBounds, &dstPixelComponents, &dstBitDepth, &dstRowBytes);

        int nComponents = dst->getPixelComponentCount();
        switch (nComponents) {
        case 1: {
            OFX::BlackFiller<float, 1> fred(*this);
            setupAndFill(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCOunt, dstBitDepth, dstRowBytes);
            break;
        }
        case 2: {
            OFX::BlackFiller<float, 2> fred(*this);
            setupAndFill(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCOunt, dstBitDepth, dstRowBytes);
            break;
        }
        case 3: {
            OFX::BlackFiller<float, 3> fred(*this);
            setupAndFill(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCOunt, dstBitDepth, dstRowBytes);
            break;
        }
        case 4: {
            OFX::BlackFiller<float, 4> fred(*this);
            setupAndFill(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCOunt, dstBitDepth, dstRowBytes);
            break;
        }
        default:
            assert(false);
            break;
        } // switch

        return;
#endif
    }

    const void *srcPixelData;
    OfxRectI srcBounds;
    OfxRectI srcRoD;
    OFX::PixelComponentEnum srcPixelComponents;
    int srcPixelComponentCount;
    OFX::BitDepthEnum srcBitDepth;
    //srcPixelBytes = getPixelBytes(srcPixelComponents, srcBitDepth);
    int srcRowBytes;
    if ( !src.get() ) {
        srcPixelData = NULL;
        srcBounds.x1 = srcBounds.y1 = srcBounds.x2 = srcBounds.y2 = 0;
        srcRoD.x1 = srcRoD.y1 = srcRoD.x2 = srcRoD.y2 = 0;
        srcPixelComponents = _srcClip ? _srcClip->getPixelComponents() : OFX::ePixelComponentNone;
        srcPixelComponentCount = _srcClip ? _srcClip->getPixelComponentCount() : 0;
        srcBitDepth = _srcClip ? _srcClip->getPixelDepth() : OFX::eBitDepthNone;
        srcRowBytes = 0;
    } else {
        assert(_srcClip);
        srcPixelData = src->getPixelData();
        srcBounds = src->getBounds();
        // = src->getRegionOfDefinition(); //  Nuke's image RoDs are wrong
        if (_supportsTiles) {
            OFX::Coords::toPixelEnclosing(_srcClip->getRegionOfDefinition(time), args.renderScale, _srcClip->getPixelAspectRatio(), &srcRoD);
        } else {
            // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
            // in hosts that do not support tiles (such as Sony Catalyst Edit), the image RoD is the image Bounds anyway.
            srcRoD = srcBounds;
        }
        srcPixelComponents = src->getPixelComponents();
        srcPixelComponentCount = src->getPixelComponentCount();
        srcBitDepth = src->getPixelDepth();
        srcRowBytes = src->getRowBytes();
    }

    void *dstPixelData = dst->getPixelData();
    const OfxRectI& dstBounds = dst->getBounds();
    OfxRectI dstRoD; // = dst->getRegionOfDefinition(); //  Nuke's image RoDs are wrong
    if (_supportsTiles) {
        OFX::Coords::toPixelEnclosing(_dstClip->getRegionOfDefinition(time), args.renderScale, _dstClip->getPixelAspectRatio(), &dstRoD);
    } else {
        // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
        // in hosts that do not support tiles (such as Sony Catalyst Edit), the image RoD is the image Bounds anyway.
        dstRoD = dstBounds;
    }
    //const OFX::PixelComponentEnum dstPixelComponents = dst->getPixelComponents();
    //const OFX::BitDepthEnum dstBitDepth = dst->getPixelDepth();
    const int dstRowBytes = dst->getRowBytes();

    if (!_supportsTiles) {
        // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsTiles
        //  If a clip or plugin does not support tiled images, then the host should supply full RoD images to the effect whenever it fetches one.
        if ( src.get() ) {
            assert(srcRoD.x1 == srcBounds.x1);
            assert(srcRoD.x2 == srcBounds.x2);
            assert(srcRoD.y1 == srcBounds.y1);
            assert(srcRoD.y2 == srcBounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
        }
        assert(dstRoD.x1 == dstBounds.x1);
        assert(dstRoD.x2 == dstBounds.x2);
        assert(dstRoD.y1 == dstBounds.y1);
        assert(dstRoD.y2 == dstBounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
    }
    if (!_supportsMultiResolution) {
        // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsMultiResolution
        //   Multiple resolution images mean...
        //    input and output images can be of any size
        //    input and output images can be offset from the origin
        if ( src.get() ) {
            assert(srcRoD.x1 == 0);
            assert(srcRoD.y1 == 0);
            assert(srcRoD.x1 == dstRoD.x1);
            assert(srcRoD.x2 == dstRoD.x2);
            assert(srcRoD.y1 == dstRoD.y1);
            assert(srcRoD.y2 == dstRoD.y2); // crashes on Natron if kSupportsMultiResolution=0
        }
    }

    bool processR, processG, processB, processA;
    if (_processR) {
        _processR->getValueAtTime(time, processR);
        _processG->getValueAtTime(time, processG);
        _processB->getValueAtTime(time, processB);
        _processA->getValueAtTime(time, processA);
    } else {
        processR = processG = processB = processA = true;
    }
    bool premult = _premult ? _premult->getValueAtTime(time) : false;
    int premultChannel = (premult && _premultChannel) ? _premultChannel->getValueAtTime(time) : 3;
    double mix = _mix->getValueAtTime(time);
    bool maskInvert = _maskInvert->getValueAtTime(time);
    if (!processR && !processG && !processB) {
        // no need to (un)premult if we don't change colors
        premult = false;
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    OFX::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    OfxRectI processWindow = renderWindow; //!< the window where pixels have to be computed (may be smaller than renderWindow if mask is zero on the borders)

    if (mix == 0.) {
        // no processing at all
        processWindow.x2 = processWindow.x1;
        processWindow.y2 = processWindow.y1;
    }
    if ( mask.get() ) {
        checkBadRenderScaleOrField(mask, args);

        if (_supportsTiles) {
            // shrink the processWindow at much as possible
            // top
            while ( processWindow.y2 > processWindow.y1 && maskLineIsZero(mask.get(), processWindow.x1, processWindow.x2, processWindow.y2 - 1, maskInvert) ) {
                --processWindow.y2;
            }
            // bottom
            while ( processWindow.y2 > processWindow.y1 && maskLineIsZero(mask.get(), processWindow.x1, processWindow.x2, processWindow.y1, maskInvert) ) {
                ++processWindow.y1;
            }
            // left
            while ( processWindow.x2 > processWindow.x1 && maskColumnIsZero(mask.get(), processWindow.x1, processWindow.y1, processWindow.y2, maskInvert) ) {
                ++processWindow.x1;
            }
            // right
            while ( processWindow.x2 > processWindow.x1 && maskColumnIsZero(mask.get(), processWindow.x2 - 1, processWindow.y1, processWindow.y2, maskInvert) ) {
                --processWindow.x2;
            }
        }
    }

    Params params;
    getValuesAtTime(time, params);
    int srcBoundary = getBoundary(params);
    assert(0 <= srcBoundary && srcBoundary <= 2);

    // copy areas of renderWindow that are not within processWindow to dst

    OfxRectI copyWindowN, copyWindowS, copyWindowE, copyWindowW;
    // top
    copyWindowN.x1 = renderWindow.x1;
    copyWindowN.x2 = renderWindow.x2;
    copyWindowN.y1 = processWindow.y2;
    copyWindowN.y2 = renderWindow.y2;
    // bottom
    copyWindowS.x1 = renderWindow.x1;
    copyWindowS.x2 = renderWindow.x2;
    copyWindowS.y1 = renderWindow.y1;
    copyWindowS.y2 = processWindow.y1;
    // left
    copyWindowW.x1 = renderWindow.x1;
    copyWindowW.x2 = processWindow.x1;
    copyWindowW.y1 = processWindow.y1;
    copyWindowW.y2 = processWindow.y2;
    // right
    copyWindowE.x1 = processWindow.x2;
    copyWindowE.x2 = renderWindow.x2;
    copyWindowE.y1 = processWindow.y1;
    copyWindowE.y2 = processWindow.y2;
    {
        OFX::auto_ptr<OFX::PixelProcessorFilterBase> fred;
        if (dstPixelComponentCount == 4) {
            fred.reset( new OFX::PixelCopier<float, 4>(*this) );
        } else if (dstPixelComponentCount == 3) {
            fred.reset( new OFX::PixelCopier<float, 3>(*this) );
        } else if (dstPixelComponentCount == 2) {
            fred.reset( new OFX::PixelCopier<float, 2>(*this) );
        }  else if (dstPixelComponentCount == 1) {
            fred.reset( new OFX::PixelCopier<float, 1>(*this) );
        }
        assert( fred.get() );
        if ( fred.get() ) {
            setupAndCopy(*fred, time, copyWindowN, renderScale, src.get(), mask.get(),
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes, srcBoundary,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                         premult, premultChannel, mix, maskInvert);
            setupAndCopy(*fred, time, copyWindowS, renderScale, src.get(), mask.get(),
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes, srcBoundary,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                         premult, premultChannel, mix, maskInvert);
            setupAndCopy(*fred, time, copyWindowW, renderScale, src.get(), mask.get(),
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes, srcBoundary,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                         premult, premultChannel, mix, maskInvert);
            setupAndCopy(*fred, time, copyWindowE, renderScale, src.get(), mask.get(),
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes, srcBoundary,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                         premult, premultChannel, mix, maskInvert);
        }
    }

    printRectI("srcRoD", srcRoD);
    printRectI("srcBounds", srcBounds);
    printRectI("dstRoD", dstRoD);
    printRectI("dstBounds", dstBounds);
    printRectI("renderWindow", renderWindow);
    printRectI("processWindow", processWindow);

    if ( OFX::Coords::rectIsEmpty(processWindow) ) {
        // the area that actually has to be processed is empty, the job is finished!
        return;
    }
    assert(mix != 0.); // mix == 0. should give an empty processWindow

    // compute the src ROI (should be consistent with getRegionsOfInterest())
    OfxRectI srcRoI;
    getRoI(processWindow, renderScale, params, &srcRoI);
    printRectI("srcRoI", srcRoI);
    // intersect against the destination RoD
    bool intersect = OFX::Coords::rectIntersection(srcRoI, dstRoD, &srcRoI);
    printRectI("srcRoIIntersected", srcRoI);
    if (!intersect) {
        src.reset(NULL);
        srcPixelData = NULL;
        srcBounds.x1 = srcBounds.y1 = srcBounds.x2 = srcBounds.y2 = 0;
        srcRoD.x1 = srcRoD.y1 = srcRoD.x2 = srcRoD.y2 = 0;
        srcPixelComponents = _srcClip->getPixelComponents();
        srcPixelComponentCount = _srcClip->getPixelComponentCount();
        srcBitDepth = _srcClip->getPixelDepth();
        srcRowBytes = 0;
    }

    // The following checks may be wrong, because the srcRoI may be outside of the region of definition of src.
    // It is not an error: areas outside of srcRoD should be considered black and transparent.
    // IF THE FOLLOWING CODE HAS TO BE DISACTIVATED, PLEASE COMMENT WHY.
    // This was disactivated by commit c47d07669b78a71960b204989d9c36f746d14a4c, then reactivated.
    // DISACTIVATED AGAIN by FD 9/12/2014: boundary conditions are now handled by pixelcopier, and interstection with dstRoD was added above
#if 0 //def CIMGFILTER_INSTERSECT_ROI
    OFX::Coords::rectIntersection(srcRoI, srcRoD, &srcRoI);
    // the resulting ROI should be within the src bounds, or it means that the host didn't take into account the region of interest (see getRegionsOfInterest() )
    assert(srcBounds.x1 <= srcRoI.x1 && srcRoI.x2 <= srcBounds.x2 &&
           srcBounds.y1 <= srcRoI.y1 && srcRoI.y2 <= srcBounds.y2);
    if ( (srcBounds.x1 > srcRoI.x1) || (srcRoI.x2 > srcBounds.x2) ||
         ( srcBounds.y1 > srcRoI.y1) || ( srcRoI.y2 > srcBounds.y2) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    if ( doMasking && (mix != 1.) ) {
        // the renderWindow should also be contained within srcBounds, since we are mixing
        assert(srcBounds.x1 <= renderWindow.x1 && renderWindow.x2 <= srcBounds.x2 &&
               srcBounds.y1 <= renderWindow.y1 && renderWindow.y2 <= srcBounds.y2);
        if ( (srcBounds.x1 > renderWindow.x1) || (renderWindow.x2 > srcBounds.x2) ||
             ( srcBounds.y1 > renderWindow.y1) || ( renderWindow.y2 > srcBounds.y2) ) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
#endif

#if cimg_use_openmp!=0
    // set the number of OpenMP threads to a reasonable value
    // (but remember that the OpenMP threads are not counted my the multithread suite)
    {
        unsigned int ncpus = OFX::MultiThread::getNumCPUs();
        omp_set_num_threads( (std::max)(1u, ncpus) );
        //printf("ncpus=%u\n", ncpus);
    }
#endif

    // from here on, we do the following steps:
    // 1- copy & unpremult all channels from srcRoI, from src to a tmp image of size srcRoI
    // 2- extract channels to be processed from tmp to a cimg of size srcRoI (and do the interleaved to coplanar conversion)
    // 3- process the cimg
    // 4- copy back the processed channels from the cImg to tmp. only processWindow has to be copied
    // 5- copy+premult+max+mix tmp to dst (only processWindow)

    //////////////////////////////////////////////////////////////////////////////////////////
    // 1- copy & unpremult all channels from srcRoI, from src to a tmp image of size srcRoI
    const OfxRectI tmpBounds = srcRoI;
    const OFX::PixelComponentEnum tmpPixelComponents = srcPixelData ? srcPixelComponents : dstPixelComponents;
    const int tmpPixelComponentCount = srcPixelData ? srcPixelComponentCount : dstPixelComponentCount;
    const OFX::BitDepthEnum tmpBitDepth = OFX::eBitDepthFloat;
    const int tmpWidth = tmpBounds.x2 - tmpBounds.x1;
    const int tmpHeight = tmpBounds.y2 - tmpBounds.y1;
    const int tmpRowBytes = tmpPixelComponentCount * getComponentBytes(tmpBitDepth) * tmpWidth;
    size_t tmpSize = (size_t)tmpRowBytes * tmpHeight;
    OFX::auto_ptr<OFX::ImageMemory> tmpData;
    float *tmpPixelData = NULL;
    if (tmpSize > 0) {
        tmpData.reset( new OFX::ImageMemory(tmpSize, this) );
        tmpPixelData = (float*)tmpData->lock();

        OFX::auto_ptr<OFX::PixelProcessorFilterBase> fred;
        if ( !src.get() ) {
            // no src, fill with black & transparent
            fred.reset( new OFX::BlackFiller<float>(*this, dstPixelComponentCount) );
        } else {
            if (dstPixelComponents == OFX::ePixelComponentRGBA) {
                fred.reset( new OFX::PixelCopierUnPremult<float, 4, 1, float, 4, 1>(*this) );
            } else if (dstPixelComponentCount == 4) {
                // just copy, no premult
                fred.reset( new OFX::PixelCopier<float, 4>(*this) );
            } else if (dstPixelComponentCount == 3) {
                // just copy, no premult
                fred.reset( new OFX::PixelCopier<float, 3>(*this) );
            } else if (dstPixelComponentCount == 2) {
                // just copy, no premult
                fred.reset( new OFX::PixelCopier<float, 2>(*this) );
            }  else if (dstPixelComponentCount == 1) {
                // just copy, no premult
                fred.reset( new OFX::PixelCopier<float, 1>(*this) );
            }
        }
        assert( fred.get() );
        if ( fred.get() ) {
            setupAndCopy(*fred, time, srcRoI, renderScale, src.get(), mask.get(),
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes, srcBoundary,
                         tmpPixelData, tmpBounds, tmpPixelComponents, tmpPixelComponentCount, tmpBitDepth, tmpRowBytes,
                         premult, premultChannel, mix, maskInvert);
        }
    }
    if ( abort() ) {
        return;
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    // 2- extract channels to be processed from tmp to a cimg of size srcRoI (and do the interleaved to coplanar conversion)

    // allocate the cimg data to hold the src ROI
    int cimgSpectrum;
    if (!_supportsComponentRemapping) {
        cimgSpectrum = tmpPixelComponentCount;
    } else {
        switch (tmpPixelComponents) {
        case OFX::ePixelComponentAlpha:
            cimgSpectrum = (int)processA;
            break;
        case OFX::ePixelComponentXY:
            cimgSpectrum = (int)processR + (int)processG + (int) processB;
            break;
        case OFX::ePixelComponentRGB:
            cimgSpectrum = (int)processR + (int)processG + (int) processB;
            break;
        case OFX::ePixelComponentRGBA:
            cimgSpectrum = (int)processR + (int)processG + (int) processB + (int)processA;
            break;
        default:
            cimgSpectrum = 0;
        }
    }
    const int cimgWidth = srcRoI.x2 - srcRoI.x1;
    const int cimgHeight = srcRoI.y2 - srcRoI.y1;
    const size_t cimgSize = (size_t)cimgWidth * cimgHeight * cimgSpectrum * sizeof(cimgpix_t);
    std::vector<int> srcChannel(cimgSpectrum, -1);

    int alphaChannel = -1;
    if (!_supportsComponentRemapping) {
        for (int c = 0; c < tmpPixelComponentCount; ++c) {
            srcChannel[c] = c;
        }
        assert(tmpPixelComponentCount == cimgSpectrum);
    } else {
        if (tmpPixelComponentCount == 1) {
            if (processA) {
                assert(cimgSpectrum == 1);
                srcChannel[0] = 0;
                alphaChannel = 0;
            } else {
                assert(cimgSpectrum == 0);
            }
        } else {
            int c = 0;
            if (processR) {
                srcChannel[c] = 0;
                ++c;
            }
            if (processG) {
                srcChannel[c] = 1;
                ++c;
            }
            if (processB) {
                srcChannel[c] = 2;
                ++c;
            }
            if ( processA && (tmpPixelComponentCount >= 4) ) {
                srcChannel[c] = 3;
                alphaChannel = c;
                ++c;
            }
            assert(c == cimgSpectrum);
        }
    }
    if (cimgSize) { // may be zero if no channel is processed
        OFX::auto_ptr<OFX::ImageMemory> cimgData( new OFX::ImageMemory(cimgSize, this) );
        cimgpix_t *cimgPixelData = (cimgpix_t*)cimgData->lock();
        cimg_library::CImg<cimgpix_t> maskcimg;
        cimg_library::CImg<cimgpix_t> cimg(cimgPixelData, cimgWidth, cimgHeight, 1, cimgSpectrum, true);

        if (tmpSize > 0) {
            for (int c = 0; c < cimgSpectrum; ++c) {
                cimgpix_t *dst = cimg.data(0, 0, 0, c);
                const float *src = tmpPixelData + srcChannel[c];
                for (unsigned int siz = cimgWidth * cimgHeight; siz; --siz, src += tmpPixelComponentCount, ++dst) {
                    *dst = *src;
                }
            }
        } else {
            cimg.fill(0);
        }
        if ( abort() ) {
            return;
        }

        assert(sizeof(cimgpix_t) == 4); // the following only works for float pix
        if (_usesMask) {
            maskcimg.assign(cimgWidth, cimgHeight, 1, 1);
            if (!mask.get()) {
                maskcimg.fill(1.);
            } else {
                copyPixels(*this,
                           srcRoI, renderScale,
                           mask.get(),
                           maskcimg.data(),
                           srcRoI,
                           OFX::ePixelComponentAlpha,
                           1,
                           OFX::eBitDepthFloat,
                           cimgWidth * sizeof(float));
                if(maskInvert) {
                    maskcimg *= -1;
                    maskcimg += 1;
                }
            }
        }

        //////////////////////////////////////////////////////////////////////////////////////////
        // 3- process the cimg
        printRectI("render srcRoI", srcRoI);
#if defined(HAVE_THREAD_LOCAL) || defined(HAVE_PTHREAD)
#  if defined(HAVE_THREAD_LOCAL)
        tls::gImageEffect = this;
#  else
        OFX::ImageEffect **_ptr = (OFX::ImageEffect **)pthread_getspecific(tls::gImageEffect_key);
        assert (NULL != _ptr);
        *_ptr = this;
#  endif
        try {
            render(args, params, srcRoI.x1, srcRoI.y1, maskcimg, cimg, alphaChannel);
        } catch (cimg_library::CImgAbortException) {
#  if defined(HAVE_THREAD_LOCAL)
            tls::gImageEffect = 0;
#  else
            *_ptr = 0;
#  endif

            return;
        }

#  if defined(HAVE_THREAD_LOCAL)
        tls::gImageEffect = 0;
#  else
        *_ptr = 0;
#  endif
#else
        render(args, params, srcRoI.x1, srcRoI.y1, maskcimg, cimg, alphaChannel);
#endif
        // check that the dimensions didn't change
        assert(cimg.width() == cimgWidth && cimg.height() == cimgHeight && cimg.depth() == 1 && cimg.spectrum() == cimgSpectrum);
        if ( abort() ) {
            return;
        }

        //////////////////////////////////////////////////////////////////////////////////////////
        // 4- copy back the processed channels from the cImg to tmp. only processWindow has to be copied

        // We copy the whole srcRoI. This could be optimized to copy only renderWindow
        for (int c = 0; c < cimgSpectrum; ++c) {
            const cimgpix_t *src = cimg.data(0, 0, 0, c);
            float *dst = tmpPixelData + srcChannel[c];
            for (unsigned int siz = cimgWidth * cimgHeight; siz; --siz, ++src, dst += tmpPixelComponentCount) {
                *dst = *src;
            }
        }
    }
    if ( abort() ) {
        return;
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    // 5- copy+premult+max+mix tmp to dst (only processWindow)

    {
        OFX::auto_ptr<OFX::PixelProcessorFilterBase> fred;
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            fred.reset( new OFX::PixelCopierPremultMaskMix<float, 4, 1, float, 4, 1>(*this) );
        } else if (dstPixelComponentCount == 4) {
            // just copy, no premult
            if (doMasking) {
                fred.reset( new OFX::PixelCopierMaskMix<float, 4, 1, true>(*this) );
            } else {
                fred.reset( new OFX::PixelCopierMaskMix<float, 4, 1, false>(*this) );
            }
        } else if (dstPixelComponentCount == 3) {
            // just copy, no premult
            if (doMasking) {
                fred.reset( new OFX::PixelCopierMaskMix<float, 3, 1, true>(*this) );
            } else {
                fred.reset( new OFX::PixelCopierMaskMix<float, 3, 1, false>(*this) );
            }
        } else if (dstPixelComponentCount == 2) {
            // just copy, no premult
            if (doMasking) {
                fred.reset( new OFX::PixelCopierMaskMix<float, 2, 1, true>(*this) );
            } else {
                fred.reset( new OFX::PixelCopierMaskMix<float, 2, 1, false>(*this) );
            }
        }  else if (dstPixelComponentCount == 1) {
            // just copy, no premult
            assert(srcPixelComponents == OFX::ePixelComponentAlpha);
            if (doMasking) {
                fred.reset( new OFX::PixelCopierMaskMix<float, 1, 1, true>(*this) );
            } else {
                fred.reset( new OFX::PixelCopierMaskMix<float, 1, 1, false>(*this) );
            }
        }
        assert( fred.get() );
        if ( fred.get() ) {
            setupAndCopy(*fred, time, processWindow, renderScale, src.get(), mask.get(),
                         tmpPixelData, tmpBounds, tmpPixelComponents, tmpPixelComponentCount, tmpBitDepth, tmpRowBytes, 0,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                         premult, premultChannel, mix, maskInvert);
        }
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    // done!
} // >::render

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
template <class Params, bool sourceIsOptional>
void
CImgFilterPluginHelper<Params, sourceIsOptional>::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                                                       OFX::RegionOfInterestSetter &rois)
{
# ifndef NDEBUG
    if ( !_supportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
# endif
    const double time = args.time;
    const OfxRectD& regionOfInterest = args.regionOfInterest;
    OfxRectD srcRoI;
    double mix = 1.;
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        _mix->getValueAtTime(time, mix);
        if (mix == 0.) {
            // identity transform
            //srcRoI = regionOfInterest;

            //rois.setRegionOfInterest(*_srcClip, srcRoI);
            return;
        }
    }

    Params params;
    getValuesAtTime(args.time, params);

    double pixelaspectratio = ( _srcClip && _srcClip->isConnected() ) ? _srcClip->getPixelAspectRatio() : 1.;
    OfxRectI rectPixel;
    OFX::Coords::toPixelEnclosing(regionOfInterest, args.renderScale, pixelaspectratio, &rectPixel);
    OfxRectI srcRoIPixel;
    getRoI(rectPixel, args.renderScale, params, &srcRoIPixel);
    OFX::Coords::toCanonical(srcRoIPixel, args.renderScale, pixelaspectratio, &srcRoI);

    if ( doMasking && (mix != 1.) ) {
        // for masking or mixing, we also need the source image.
        // compute the bounding box with the default ROI
        OFX::Coords::rectBoundingBox(srcRoI, regionOfInterest, &srcRoI);
    }

    // no need to set it on mask (the default ROI is OK)
    rois.setRegionOfInterest(*_srcClip, srcRoI);
}

template <class Params, bool sourceIsOptional>
bool
CImgFilterPluginHelper<Params, sourceIsOptional>::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                                                        OfxRectD &rod)
{
# ifndef NDEBUG
    if ( !_supportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
# endif
    Params params;
    getValuesAtTime(args.time, params);

    OfxRectI srcRoDPixel = {0, 0, 0, 0};
    {
        double pixelaspectratio = ( _srcClip && _srcClip->isConnected() ) ? _srcClip->getPixelAspectRatio() : 1.;
        if (_srcClip) {
            OFX::Coords::toPixelEnclosing(_srcClip->getRegionOfDefinition(args.time), args.renderScale, pixelaspectratio, &srcRoDPixel);
        }
    }
    OfxRectI rodPixel;
    bool ret = getRegionOfDefinition(srcRoDPixel, args.renderScale, params, &rodPixel);
    if (ret) {
        double pixelaspectratio = _dstClip ? _dstClip->getPixelAspectRatio() : 1.;
        OFX::Coords::toCanonical(rodPixel, args.renderScale, pixelaspectratio, &rod);

        return true;
    }

    return false;
}

template <class Params, bool sourceIsOptional>
bool
CImgFilterPluginHelper<Params, sourceIsOptional>::isIdentity(const OFX::IsIdentityArguments &args,
                                                             OFX::Clip * &identityClip,
                                                             double & /*identityTime*/
                                                             , int& /*view*/, std::string& /*plane*/)
{
# ifndef NDEBUG
    if ( !_supportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
# endif
    const double time = args.time;
    double mix;
    _mix->getValueAtTime(time, mix);
    if (mix == 0.) {
        identityClip = _srcClip;

        return true;
    }

    if (_processR) {
        bool processR;
        bool processG;
        bool processB;
        bool processA;
        _processR->getValueAtTime(args.time, processR);
        _processG->getValueAtTime(args.time, processG);
        _processB->getValueAtTime(args.time, processB);
        _processA->getValueAtTime(args.time, processA);
        if (!processR && !processG && !processB && !processA) {
            identityClip = _srcClip;

            return true;
        }
    }

    Params params;
    getValuesAtTime(time, params);
    if ( isIdentity(args, params) ) {
        identityClip = _srcClip;

        return true;
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            if (OFX::getImageEffectHostDescription()->supportsMultiResolution) {
                // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
                // In hosts that do not support multiResolution (e.g. Sony Catalyst Edit), all inputs have the same RoD anyway.
                OFX::Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
                // effect is identity if the renderWindow doesn't intersect the mask RoD
                if ( !OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                    identityClip = _srcClip;

                    return true;
                }
            }
        }
    }

    return false;
} // >::isIdentity

// functions for a reproductible random number generator (used in CImgNoise.cpp and CImgPlasma.cpp)
inline unsigned int
cimg_hash(unsigned int a)
{
    a = (a ^ 61) ^ (a >> 16);
    a = a + (a << 3);
    a = a ^ (a >> 4);
    a = a * 0x27d4eb2d;
    a = a ^ (a >> 15);

    return a;
}

// returns a value from 0 to 0x100000000ULL excluded
inline unsigned int
cimg_irand(unsigned int seed, int x, int y, int nComponents)
{
    return cimg_hash(cimg_hash(cimg_hash(seed ^ x) ^ y) ^ nComponents);
}

inline double
cimg_rand(unsigned int seed, int x, int y, int nComponents, const double val_min, const double val_max)
{
    const double val = cimg_irand(seed, x, y, nComponents) / ( (double)0x100000000ULL );
    return val_min + (val_max - val_min)*val;
}

//! Return a random variable uniformely distributed between [0,val_max].
/**
 **/
inline double
cimg_rand(unsigned int seed, int x, int y, int nComponents, const double val_max = 1.)
{
    return cimg_rand(seed, x, y, nComponents, 0, val_max);
}

//! Return a random variable following a gaussian distribution and a standard deviation of 1.
/**
 **/
inline double
cimg_grand(unsigned int seed, int x, int y, int nComponents)
{
    double x1, w;
    unsigned int s = seed;
    do {
        unsigned int r1 = cimg_irand(s, x, y, nComponents);
        unsigned int r2 = cimg_irand(r1, x, y, nComponents);
        s = r2;

        const double x2 =  2 * (double) r2 / ( (double)0x100000000ULL ) - 1.;
        x1 =  2 * (double) r1 / ( (double)0x100000000ULL ) - 1.;
        w = x1*x1 + x2*x2;
    } while (w<=0 || w>=1.0);
    return x1*std::sqrt((-2*std::log(w))/w);
}

//! Return a random variable following a Poisson distribution of parameter z.
/**
 **/
inline unsigned int
cimg_prand(unsigned int seed, int x, int y, int nComponents, const double z)
{
    if (z<=1.0e-10) {
        return 0;
    }
    if (z>100) {
        return (unsigned int)((std::sqrt(z) * cimg_grand(seed, x, y, nComponents)) + z);
    }
    unsigned int k = 0;
    const double y1 = std::exp(-z);
    for (double s = 1.0; s >= y1; ++k) {
        s *= cimg_rand(seed+1, x, y, nComponents);
    }
    return k > 0 ? k - 1 : 0;
}

#endif // ifndef Misc_CImgFilter_h
