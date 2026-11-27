/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
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

/*
 * OFX ColorMatrix plugin.
 */

#include <array>
#include <cmath>
#include <cstring>
#include <optional>

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMatrix2D.h"
#include "ofxsRGBAProcessing.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ColorMatrixOFX"
#define kPluginGrouping "Color/Math"
#define kPluginDescription "Multiply the RGBA channels by an arbitrary 4x4 matrix."
#define kPluginIdentifier "net.sf.openfx.ColorMatrixPlugin"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
// version 2.1: NaN/inf-safe premultiply (SupportExt #73, #79);
//              Invert parameter, off by default: apply the inverse of the matrix (issue #291)
// version 3.0: Clamp Black is off by default, so that the default node keeps negative values (issue #272).
//              Version 2.1 is kept, obsolete.
#define kPluginVersionMajor 3 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginVersionMinorV2 1 // the minor version of the obsolete version 2

// the first major version whose Clamp Black is off by default (issue #272)
static constexpr unsigned kVersionClampBlackOff = 3;

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#ifdef OFX_EXTENSIONS_NATRON
#define kParamProcessR kNatronOfxParamProcessR
#define kParamProcessRLabel kNatronOfxParamProcessRLabel
#define kParamProcessRHint kNatronOfxParamProcessRHint
#define kParamProcessG kNatronOfxParamProcessG
#define kParamProcessGLabel kNatronOfxParamProcessGLabel
#define kParamProcessGHint kNatronOfxParamProcessGHint
#define kParamProcessB kNatronOfxParamProcessB
#define kParamProcessBLabel kNatronOfxParamProcessBLabel
#define kParamProcessBHint kNatronOfxParamProcessBHint
#define kParamProcessA kNatronOfxParamProcessA
#define kParamProcessALabel kNatronOfxParamProcessALabel
#define kParamProcessAHint kNatronOfxParamProcessAHint
#else
#define kParamProcessR      "processR"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red channel."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green channel."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue channel."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha channel."
#endif

#define kParamOutputRedName  "outputRed"
#define kParamOutputRedLabel "Output Red"
#define kParamOutputRedHint  "values for red output component."

#define kParamOutputGreenName  "outputGreen"
#define kParamOutputGreenLabel "Output Green"
#define kParamOutputGreenHint  "values for green output component."

#define kParamOutputBlueName  "outputBlue"
#define kParamOutputBlueLabel "Output Blue"
#define kParamOutputBlueHint  "values for blue output component."

#define kParamOutputAlphaName  "outputAlpha"
#define kParamOutputAlphaLabel "Output Alpha"
#define kParamOutputAlphaHint  "values for alpha output component."

#define kParamInvert "invert"
#define kParamInvertLabel "Invert"
#define kParamInvertHint "When checked, the inverse of the conversion is applied, so that a ColorMatrix with the same matrix and Invert checked undoes it. " \
    "The conversion is the 4x4 matrix, where the channels that are not processed (or not in the image) are left unchanged, as by the render without Invert. " \
    "If it is not invertible (or so close to singular that its inverse is meaningless), the render fails. " \
    "Clamp Black sets negative values to 0: a round trip through two ColorMatrix nodes gives back negative values only if it is unchecked on both."

#define kParamClampBlack "clampBlack"
#define kParamClampBlackLabel "Clamp Black"
#define kParamClampBlackHint "All colors below 0 on output are set to 0."

#define kParamClampWhite "clampWhite"
#define kParamClampWhiteLabel "Clamp White"
#define kParamClampWhiteHint "All colors above 1 on output are set to 1."

#define kParamPremultChanged "premultChanged"

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif


struct RGBAValues
{
    double r, g, b, a;
    RGBAValues(double v) : r(v), g(v), b(v), a(v) {}

    RGBAValues() : r(0), g(0), b(0), a(0) {}
};

// the rows of the colour matrix: Output Red, Green, Blue and Alpha
using ColorMatrixRows = std::array<RGBAValues, 4>;

// the channels (R, G, B, A) that the render changes: processed, and in the image
using ColorMatrixChannels = std::array<bool, 4>;

// the matrix whose rows are m
static Matrix4x4
toMatrix4x4(const ColorMatrixRows& m)
{
    Matrix4x4 mat;
    for (int i = 0; i < 4; ++i) {
        mat(i, 0) = m[i].r;
        mat(i, 1) = m[i].g;
        mat(i, 2) = m[i].b;
        mat(i, 3) = m[i].a;
    }

    return mat;
}

// the rows of mat
static ColorMatrixRows
toRows(const Matrix4x4& mat)
{
    ColorMatrixRows m;
    for (int i = 0; i < 4; ++i) {
        m[i].r = mat(i, 0);
        m[i].g = mat(i, 1);
        m[i].b = mat(i, 2);
        m[i].a = mat(i, 3);
    }

    return m;
}

// The inverse of mat, or nothing if it is not invertible. A matrix whose determinant is tiny relative to
// the product of its row norms (an upper bound of the determinant, Hadamard's inequality) is not invertible
// either: its inverse would only amplify rounding errors (e.g. rows (.1,.2,.3), (.4,.5,.6), (.7,.8,.9) have a
// determinant of about 1e-17 in double, and an inverse with entries around 1e15).
std::optional<ColorMatrixRows>
invertColorMatrix(const Matrix4x4& mat)
{
    constexpr double kRelativeDeterminantMin = 1e-12;
    double rowNormProduct = 1.;
    for (int i = 0; i < 4; ++i) {
        rowNormProduct *= std::sqrt(mat(i, 0) * mat(i, 0) + mat(i, 1) * mat(i, 1) + mat(i, 2) * mat(i, 2) + mat(i, 3) * mat(i, 3));
    }
    const double det = mat.determinant();
    // also false for a NaN determinant or row norm
    if ( !( std::abs(det) > kRelativeDeterminantMin * rowNormProduct ) ) {
        return std::nullopt;
    }
    // inverse() also fails if an entry of the inverse overflows: the determinant test only bounds
    // them by 1 / (kRelativeDeterminantMin * the norm of a row), which overflows for a tiny row
    Matrix4x4 inv;
    if ( !mat.inverse(&inv) ) {
        return std::nullopt;
    }

    return toRows(inv);
}

// The matrix that the render applies: the rows of the channels it does not change are unit
// rows (the input value is kept), the other rows are those of m.
Matrix4x4
effectiveColorMatrix(const ColorMatrixRows& m,
                     const ColorMatrixChannels& changed)
{
    Matrix4x4 mat = toMatrix4x4(m);
    for (int i = 0; i < 4; ++i) {
        if (!changed[i]) {
            for (int j = 0; j < 4; ++j) {
                mat(i, j) = (i == j) ? 1. : 0.;
            }
        }
    }

    return mat;
}

// true if the rows of the processed channels are unit rows: the render keeps these channels
static bool
isIdentityOn(const ColorMatrixRows& m,
             const ColorMatrixChannels& processed)
{
    const Matrix4x4 mat = toMatrix4x4(m);
    for (int i = 0; i < 4; ++i) {
        if (!processed[i]) {
            continue;
        }
        for (int j = 0; j < 4; ++j) {
            if ( mat(i, j) != ( (i == j) ? 1. : 0. ) ) {
                return false;
            }
        }
    }

    return true;
}

class ColorMatrixProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    ColorMatrixRows _matrix;
    bool _clampBlack;
    bool _clampWhite;
    bool _premult;
    int _premultChannel;
    bool _doMasking;
    double _mix;
    bool _maskInvert;

public:

    ColorMatrixProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _processR(true)
        , _processG(true)
        , _processB(true)
        , _processA(false)
        , _clampBlack(true)
        , _clampWhite(true)
        , _premult(false)
        , _premultChannel(3)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setMaskImg(const Image *v,
                    bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   const ColorMatrixRows& matrix,
                   bool clampBlack,
                   bool clampWhite,
                   bool premult,
                   int premultChannel,
                   double mix)
    {
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        _matrix = matrix;
        _clampBlack = clampBlack;
        _clampWhite = clampWhite;
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
    }

private:
};


template <class PIX, int nComponents, int maxValue>
class ColorMatrixProcessor
    : public ColorMatrixProcessorBase
{
public:
    ColorMatrixProcessor(ImageEffect &instance)
        : ColorMatrixProcessorBase(instance)
    {
    }

private:

    double apply(int c,
                 double inR,
                 double inG,
                 double inB,
                 double inA)
    {
        double comp = _matrix[c].r * inR + _matrix[c].g * inG + _matrix[c].b * inB + _matrix[c].a * inA;

        if ( _clampBlack && (comp < 0.) ) {
            comp = 0.;
        } else if ( _clampWhite && (comp > 1.) ) {
            // values are normalized to [0,1]
            comp = 1.;
        }

        return comp;
    }

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        ofxsProcessRGBA<nComponents>(*this, _processR, _processG, _processB, _processA, procWindow, rs);
    } // multiThreadProcessImages

public:
    // called by ofxsProcessRGBA()
    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow, const OfxPointD& rs)
    {
        unused(rs);
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float unpPix[4] = {0.f, 0.f, 0.f, 0.f};
        float tmpPix[4] = {0.f, 0.f, 0.f, 0.f};
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                for (int c = 0; c < 4; ++c) { // tmpPix has 4 components
                    if ( ( processR && (c == 0) ) ||
                         ( processG && ( c == 1) ) ||
                         ( processB && ( c == 2) ) ||
                         ( processA && ( c == 3) ) ) {
                        tmpPix[c] = (float)apply(c, unpPix[0], unpPix[1], unpPix[2], unpPix[3]);
                    } else {
                        tmpPix[c] = unpPix[c];
                    }
                }
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // copy back original values from unprocessed channels
                ofxsCopyBackUnprocessedChannels<PIX, nComponents, processR, processG, processB, processA>(srcPix, dstPix);
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // process
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ColorMatrixPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    ColorMatrixPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _premultChanged(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _outputRed = fetchRGBAParam(kParamOutputRedName);
        _outputGreen = fetchRGBAParam(kParamOutputGreenName);
        _outputBlue = fetchRGBAParam(kParamOutputBlueName);
        _outputAlpha = fetchRGBAParam(kParamOutputAlphaName);
        _invert = fetchBooleanParam(kParamInvert);
        assert(_invert);
        _clampBlack = fetchBooleanParam(kParamClampBlack);
        _clampWhite = fetchBooleanParam(kParamClampWhite);
        assert(_outputRed && _outputGreen && _outputBlue && _outputAlpha && _clampBlack && _clampWhite);
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(ColorMatrixProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /// The channels that the render changes at time t (processed, and in images of the
    /// components comps).
    ColorMatrixChannels getChangedChannels(double time, PixelComponentEnum comps) const;

    /// The matrix applied at time t to the channels changed: the parameters, or with Invert, the
    /// inverse of the conversion that the render applies without Invert (the other channels
    /// unchanged); nothing if Invert is checked and that conversion is not invertible.
    std::optional<ColorMatrixRows> getMatrix(double time, const ColorMatrixChannels& changed) const;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    RGBAParam *_outputRed;
    RGBAParam *_outputGreen;
    RGBAParam *_outputBlue;
    RGBAParam *_outputAlpha;
    BooleanParam* _invert;
    BooleanParam* _clampBlack;
    BooleanParam* _clampWhite;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    BooleanParam* _premultChanged; // set to true the first time the user connects src
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
ColorMatrixPlugin::setupAndProcess(ColorMatrixProcessorBase &processor,
                                   const RenderArguments &args)
{
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
# ifndef NDEBUG
    checkBadRenderScaleOrField(dst, args);
# endif
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
    if ( src.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(src, args);
# endif
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    // do we do masking
    if (doMasking) {
        if ( mask.get() ) {
            checkBadRenderScaleOrField(mask, args);
        }
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);
    const std::optional<ColorMatrixRows> matrix = getMatrix( args.time, getChangedChannels(args.time, dstComponents) );
    if (!matrix) {
        setPersistentMessage(Message::eMessageError, "", "Invert is checked, but the matrix is not invertible");
        throwSuiteStatusException(kOfxStatFailed);
    }
    clearPersistentMessage();
    bool clampBlack, clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    processor.setValues(processR, processG, processB, processA,
                        *matrix, clampBlack, clampWhite, premult, premultChannel, mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // ColorMatrixPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
ColorMatrixPlugin::renderInternal(const RenderArguments &args,
                                  BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        ColorMatrixProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        ColorMatrixProcessor<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        ColorMatrixProcessor<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
ColorMatrixPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(OFX_COMPONENTS_OK(dstComponents));
    // do the rendering
    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
#endif
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

bool
ColorMatrixPlugin::isIdentity(const IsIdentityArguments &args,
                              Clip * &identityClip,
                              double & /*identityTime*/
                              , int& /*view*/, std::string& /*plane*/)
{
    double mix;

    _mix->getValueAtTime(args.time, mix);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;

        return true;
    }

    bool clampBlack, clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    if (clampBlack || clampWhite) {
        return false;
    }
    bool processR, processG, processB, processA;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);
    const std::optional<ColorMatrixRows> matrix = getMatrix( args.time, getChangedChannels( args.time, _dstClip->getPixelComponents() ) );
    // not invertible: the render reports the error, except where the mask excludes the render window (below)
    if ( matrix && isIdentityOn( *matrix, ColorMatrixChannels{ {processR, processG, processB, processA} } ) ) {
        identityClip = _srcClip;

        return true;
    }

    if ( ofxsMaskRoDExcludesRenderWindow(args, _maskClip, _maskApply, _maskInvert) ) {
        identityClip = _srcClip;

        return true;
    }

    return false;
} // ColorMatrixPlugin::isIdentity

ColorMatrixChannels
ColorMatrixPlugin::getChangedChannels(double time,
                                      PixelComponentEnum comps) const
{
    const bool hasRGB = (comps != ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
    const bool hasB = hasRGB && (comps != ePixelComponentXY);
#else
    const bool hasB = hasRGB;
#endif
    const bool hasA = (comps == ePixelComponentRGBA) || (comps == ePixelComponentAlpha);

    return ColorMatrixChannels{ { hasRGB && _processR->getValueAtTime(time),
                                  hasRGB && _processG->getValueAtTime(time),
                                  hasB && _processB->getValueAtTime(time),
                                  hasA && _processA->getValueAtTime(time) } };
}

std::optional<ColorMatrixRows>
ColorMatrixPlugin::getMatrix(double time,
                             const ColorMatrixChannels& changed) const
{
    ColorMatrixRows m;
    _outputRed->getValueAtTime(time, m[0].r, m[0].g, m[0].b, m[0].a);
    _outputGreen->getValueAtTime(time, m[1].r, m[1].g, m[1].b, m[1].a);
    _outputBlue->getValueAtTime(time, m[2].r, m[2].g, m[2].b, m[2].a);
    _outputAlpha->getValueAtTime(time, m[3].r, m[3].g, m[3].b, m[3].a);
    if ( !_invert->getValueAtTime(time) ) {
        return m;
    }

    return invertColorMatrix( effectiveColorMatrix(m, changed) );
}

void
ColorMatrixPlugin::changedClip(const InstanceChangedArgs &args,
                               const std::string &clipName)
{
    ofxsPremultUpdateFromSourceClip(args, clipName, _srcClip, _premult, _premultChanged);
}

void
ColorMatrixPlugin::changedParam(const InstanceChangedArgs &args,
                                const std::string &paramName)
{
    if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    }
}

mDeclarePluginFactoryVersioned(ColorMatrixPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
ColorMatrixPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    desc.setIsObsolete(majorVersion < kPluginVersionMajor);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
}

template<unsigned int majorVersion>
void
ColorMatrixPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                                          ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamOutputRedName);
        param->setLabel(kParamOutputRedLabel);
        param->setHint(kParamOutputRedHint);
        param->setDefault(1.0, 0.0, 0.0, 0.0);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }
    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamOutputGreenName);
        param->setLabel(kParamOutputGreenLabel);
        param->setHint(kParamOutputGreenHint);
        param->setDefault(0.0, 1.0, 0.0, 0.0);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }
    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamOutputBlueName);
        param->setLabel(kParamOutputBlueLabel);
        param->setHint(kParamOutputBlueHint);
        param->setDefault(0.0, 0.0, 1.0, 0.0);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }
    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamOutputAlphaName);
        param->setLabel(kParamOutputAlphaLabel);
        param->setHint(kParamOutputAlphaHint);
        param->setDefault(0.0, 0.0, 0.0, 1.0);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamInvert);
        param->setLabel(kParamInvertLabel);
        param->setHint(kParamInvertHint);
        param->setDefault(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampBlack);
        param->setLabel(kParamClampBlackLabel);
        param->setHint(kParamClampBlackHint);
        param->setDefault(majorVersion < kVersionClampBlackOff);
        param->setAnimates(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 0);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampWhite);
        param->setLabel(kParamClampWhiteLabel);
        param->setHint(kParamClampWhiteHint);
        param->setDefault(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsMaskDescribeParams(desc, page);
    ofxsPremultDescribeParams(desc, page);
    ofxsMixDescribeParams(desc, page);

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremultChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // ColorMatrixPluginFactory::describeInContext

template<unsigned int majorVersion>
ImageEffect*
ColorMatrixPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                       ContextEnum /*context*/)
{
    return new ColorMatrixPlugin(handle);
}

static ColorMatrixPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
// version 2: Clamp Black is on by default
static ColorMatrixPluginFactory<2> p_v2(kPluginIdentifier, kPluginVersionMinorV2);
mRegisterPluginFactoryInstance(p)
mRegisterPluginFactoryInstance(p_v2)

OFXS_NAMESPACE_ANONYMOUS_EXIT
