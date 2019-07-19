/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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
 * OFX Quantize plugin.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>
//#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"

//#define USE_RANDOMGENERATOR // randomGenerator is more than 10 times slower than our pseudo-random hash
#ifdef USE_RANDOMGENERATOR
#include "randomGenerator.H"
#else
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#define uint32_t unsigned int
#else
#include <stdint.h> // for uint32_t
#endif
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "Quantize"
#define kPluginGrouping "Color"
#define kPluginDescription "Reduce the number of color levels per channel.\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=Color_Operation"

#define kPluginIdentifier "net.sf.openfx.Quantize"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

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
#define kParamProcessRHint  "Process red component."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green component."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue component."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha component."
#endif

#define kParamColors "colors"
#define kParamColorsLabel "Colors"
#define kParamColorsHint "Number of color levels to use per channel."
#define kParamColorsDefault 16
#define kParamColorsMin 2
#define kParamColorsMax 256

// a great resource about dithering: http://bisqwit.iki.fi/story/howto/dither/jy/
#define kParamDither "dither"
#define kParamDitherLabel "Dither"
#define kParamDitherHint "Dithering method to apply in order to avoid the banding effect."
#define kParamDitherOptionNone "None", "No dithering (posterize), creating abrupt changes.", "none"
#define kParamDitherOptionOrderedBayer2 "Ordered (Bayer 2x2)", "Ordered dithering using a 2x2 Bayer matrix.", "bayer2x2"
#define kParamDitherOptionOrderedBayer4 "Ordered (Bayer 4x4)", "Ordered dithering using a 4x4 Bayer matrix.", "bayer4x4"
#define kParamDitherOptionOrderedBayer8 "Ordered (Bayer 8x8)", "Ordered dithering using a 8x8 Bayer matrix.", "bayer8x8"
#define kParamDitherOptionOrderedVoidAndCluster14 "Ordered (void-and-cluster 14x14)", "Ordered dithering using a void-and-cluster 14x14 matrix.", "vac14x14"
#define kParamDitherOptionOrderedVoidAndCluster25 "Ordered (void-and-cluster 25x25)", "Ordered dithering using a void-and-cluster 25x25 matrix.", "vac25x25"
#define kParamDitherOptionRandom "Random", "Random dithering.", "random"
enum DitherEnum
{
    eDitherNone = 0,
    eDitherOrderedBayer2,
    eDitherOrderedBayer4,
    eDitherOrderedBayer8,
    eDitherOrderedVAC14,
    eDitherOrderedVAC25,
    eDitherRandom,
};

#define kParamSeed "seed"
#define kParamSeedLabel "Seed"
#define kParamSeedHint "Random seed: change this if you want different instances to have different dithering (only for random dithering)."

#define kParamStaticSeed "staticSeed"
#define kParamStaticSeedLabel "Static Seed"
#define kParamStaticSeedHint "When enabled, the dither pattern remains the same for every frame producing a constant dither effect."

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif


// void-and-cluster matrices from http://caca.zoy.org/study/part2.html
static const unsigned short vac14[14][14] = {
    {131, 187, 8, 78, 50, 18, 134, 89, 155, 102, 29, 95, 184, 73},
    {22, 86, 113, 171, 142, 105, 34, 166, 9, 60, 151, 128, 40, 110},
    {168, 137, 45, 28, 64, 188, 82, 54, 124, 189, 80, 13, 156, 56},
    {7, 61, 186, 121, 154, 6, 108, 177, 24, 100, 38, 176, 93, 123},
    {83, 148, 96, 17, 88, 133, 44, 145, 69, 161, 139, 72, 30, 181},
    {115, 27, 163, 47, 178, 65, 164, 14, 120, 48, 5, 127, 153, 52},
    {190, 58, 126, 81, 116, 21, 106, 77, 173, 92, 191, 63, 99, 12},
    {76, 144, 4, 185, 37, 149, 192, 39, 135, 23, 117, 31, 170, 132},
    {35, 172, 103, 66, 129, 79, 3, 97, 57, 159, 70, 141, 53, 94},
    {114, 20, 49, 158, 19, 146, 169, 122, 183, 11, 104, 180, 2, 165},
    {152, 87, 182, 118, 91, 42, 67, 25, 84, 147, 43, 85, 125, 68},
    {16, 136, 71, 10, 193, 112, 160, 138, 51, 111, 162, 26, 194, 46},
    {174, 107, 41, 143, 33, 74, 1, 101, 195, 15, 75, 140, 109, 90},
    {32, 62, 157, 98, 167, 119, 179, 59, 36, 130, 175, 55, 0, 150}
};
static const unsigned short vac25[25][25] = {
    {165, 530, 106, 302, 540, 219, 477, 100, 231, 417, 314, 223, 424, 37, 207, 434, 326, 22, 448, 338, 111, 454, 523, 278, 579},
    {334, 19, 410, 495, 57, 352, 158, 318, 598, 109, 509, 157, 524, 282, 606, 83, 225, 539, 163, 234, 607, 313, 206, 71, 470},
    {251, 608, 216, 135, 275, 609, 415, 29, 451, 204, 397, 21, 373, 107, 462, 348, 482, 120, 362, 508, 33, 147, 572, 388, 142},
    {447, 77, 345, 565, 439, 104, 215, 546, 279, 69, 567, 311, 585, 258, 177, 17, 266, 601, 55, 428, 270, 461, 331, 26, 560},
    {164, 271, 486, 186, 16, 336, 457, 150, 342, 471, 245, 161, 56, 396, 496, 555, 385, 146, 321, 190, 526, 97, 182, 511, 297},
    {429, 553, 49, 374, 536, 263, 575, 43, 501, 124, 368, 538, 450, 121, 309, 84, 210, 449, 561, 79, 356, 610, 256, 378, 58},
    {105, 315, 156, 244, 423, 118, 183, 408, 220, 611, 15, 198, 293, 596, 221, 375, 581, 39, 238, 500, 287, 14, 437, 139, 595},
    {227, 403, 590, 478, 68, 612, 295, 517, 87, 312, 413, 515, 78, 433, 13, 476, 134, 340, 414, 160, 466, 213, 547, 324, 456},
    {542, 141, 12, 335, 214, 357, 11, 381, 242, 469, 159, 265, 383, 176, 545, 285, 197, 503, 108, 576, 51, 187, 98, 200, 34},
    {358, 489, 277, 570, 96, 441, 554, 123, 534, 52, 556, 112, 605, 330, 70, 392, 613, 28, 288, 361, 232, 602, 300, 502, 267},
    {102, 195, 399, 152, 484, 264, 166, 289, 427, 192, 298, 407, 25, 249, 520, 114, 233, 444, 543, 170, 498, 131, 452, 66, 562},
    {310, 586, 54, 531, 346, 42, 614, 354, 23, 588, 491, 151, 468, 353, 187, 483, 369, 153, 85, 425, 10, 276, 371, 174, 420},
    {32, 459, 222, 304, 136, 421, 103, 458, 230, 339, 67, 260, 578, 93, 544, 9, 280, 594, 327, 248, 582, 472, 50, 615, 254},
    {537, 359, 91, 600, 475, 212, 525, 168, 558, 128, 455, 370, 179, 301, 405, 209, 467, 48, 442, 127, 355, 184, 332, 481, 126},
    {286, 175, 436, 273, 31, 377, 306, 36, 412, 294, 616, 8, 473, 60, 603, 116, 347, 532, 191, 568, 61, 522, 90, 218, 391},
    {592, 62, 514, 122, 552, 149, 617, 241, 513, 81, 202, 272, 557, 333, 226, 507, 255, 72, 305, 402, 229, 418, 296, 551, 7},
    {411, 317, 236, 416, 337, 480, 64, 389, 132, 350, 487, 404, 89, 162, 435, 44, 419, 618, 113, 505, 20, 604, 138, 465, 188},
    {493, 133, 580, 6, 169, 259, 320, 548, 193, 593, 40, 178, 512, 364, 591, 144, 319, 196, 386, 261, 351, 205, 384, 76, 269},
    {38, 349, 208, 504, 440, 99, 490, 5, 426, 243, 322, 574, 281, 4, 237, 460, 527, 3, 549, 155, 577, 47, 533, 316, 619},
    {394, 519, 82, 268, 325, 566, 199, 299, 119, 529, 75, 400, 125, 492, 344, 86, 217, 308, 463, 80, 395, 284, 474, 117, 201},
    {95, 235, 422, 620, 143, 45, 372, 597, 453, 343, 185, 479, 247, 569, 171, 409, 584, 129, 365, 239, 488, 94, 224, 438, 559},
    {283, 541, 18, 194, 401, 516, 262, 148, 41, 250, 621, 24, 329, 92, 446, 27, 291, 485, 35, 622, 180, 535, 379, 30, 341},
    {443, 145, 363, 494, 246, 101, 445, 550, 390, 499, 115, 432, 521, 211, 623, 253, 528, 189, 430, 307, 53, 323, 130, 624, 172},
    {46, 589, 292, 63, 599, 328, 203, 74, 290, 181, 376, 274, 140, 393, 59, 367, 88, 380, 137, 506, 252, 571, 431, 240, 497},
    {382, 228, 464, 167, 398, 2, 573, 366, 518, 1, 583, 73, 563, 303, 510, 154, 564, 257, 587, 65, 406, 173, 0, 360, 110}
};
static const unsigned char bayer8[8][8] = {
    { 0, 32, 8, 40, 2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43, 1, 33, 9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21}
};
static const unsigned char bayer4[4][4] = {
    {5,   9,   6,   10},
    {13,   1,   14,   2},
    {7,   11,   4,   8},
    {15,   3,   12,   0}
};
static const unsigned char bayer2[2][2] = {
    {1, 2},
    {3, 0}
};
static unsigned int
hash(unsigned int a)
{
    a = (a ^ 61) ^ (a >> 16);
    a = a + (a << 3);
    a = a ^ (a >> 4);
    a = a * 0x27d4eb2d;
    a = a ^ (a >> 15);

    return a;
}

using namespace OFX;

class QuantizeProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _premult;
    int _premultChannel;
    bool _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR, _processG, _processB, _processA;
    double _colors;
    DitherEnum _dither;
    uint32_t _seed;       // base seed

public:
    QuantizeProcessorBase(ImageEffect &instance,
                          const RenderArguments & /*args*/)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _premult(false)
        , _premultChannel(3)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _processR(false)
        , _processG(false)
        , _processB(false)
        , _processA(false)
        , _colors(kParamColorsDefault)
        , _dither(eDitherNone)
        , _seed(0)
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setMaskImg(const Image *v,
                    bool maskInvert) {_maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(bool premult,
                   int premultChannel,
                   double mix,
                   bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   double colors,
                   DitherEnum dither,
                   uint32_t seed)
    {
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        _colors = colors;
        _dither = dither;
        _seed = seed;
    }
};


template <class PIX, int nComponents, int maxValue>
class QuantizeProcessor
    : public QuantizeProcessorBase
{
public:
    QuantizeProcessor(ImageEffect &instance,
                      const RenderArguments &args)
        : QuantizeProcessorBase(instance, args)
    {
        //const double time = args.time;

        // TODO: any pre-computation goes here (such as computing a LUT)
    }

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
#     ifndef __COVERITY__ // too many coverity[dead_error_line] errors
        const bool r = _processR && (nComponents != 1);
        const bool g = _processG && (nComponents >= 2);
        const bool b = _processB && (nComponents >= 3);
        const bool a = _processA && (nComponents == 1 || nComponents == 4);
        if (r) {
            if (g) {
                if (b) {
                    if (a) {
                        return process<true, true, true, true >(procWindow, rs); // RGBA
                    } else {
                        return process<true, true, true, false>(procWindow, rs); // RGBa
                    }
                } else {
                    if (a) {
                        return process<true, true, false, true >(procWindow, rs); // RGbA
                    } else {
                        return process<true, true, false, false>(procWindow, rs); // RGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<true, false, true, true >(procWindow, rs); // RgBA
                    } else {
                        return process<true, false, true, false>(procWindow, rs); // RgBa
                    }
                } else {
                    if (a) {
                        return process<true, false, false, true >(procWindow, rs); // RgbA
                    } else {
                        return process<true, false, false, false>(procWindow, rs); // Rgba
                    }
                }
            }
        } else {
            if (g) {
                if (b) {
                    if (a) {
                        return process<false, true, true, true >(procWindow, rs); // rGBA
                    } else {
                        return process<false, true, true, false>(procWindow, rs); // rGBa
                    }
                } else {
                    if (a) {
                        return process<false, true, false, true >(procWindow, rs); // rGbA
                    } else {
                        return process<false, true, false, false>(procWindow, rs); // rGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<false, false, true, true >(procWindow, rs); // rgBA
                    } else {
                        return process<false, false, true, false>(procWindow, rs); // rgBa
                    }
                } else {
                    if (a) {
                        return process<false, false, false, true >(procWindow, rs); // rgbA
                    } else {
                        return process<false, false, false, false>(procWindow, rs); // rgba
                    }
                }
            }
        }
#     endif // ifndef __COVERITY__
    } // multiThreadProcessImages

private:


    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow, const OfxPointD& rs)
    {
        unused(rs);
        assert( (!processR && !processG && !processB) || (nComponents == 3 || nComponents == 4) );
        assert( !processA || (nComponents == 1 || nComponents == 4) );
        assert(nComponents == 3 || nComponents == 4);
        float unpPix[4];
        float tmpPix[4];
        // set up a random number generator and set the seed
#ifdef USE_RANDOMGENERATOR
        RandomGenerator randy;
#endif
        double randValue;
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                // for a given x,y position, the output should always be the same.
                if (_dither == eDitherRandom) {
#                 ifdef USE_RANDOMGENERATOR
                    randy.reseed(hash(x + 0x10000 * _seed) + y);
                    randValue = randy.random();
#                 else
                    randValue = hash(hash(hash(_seed ^ x) ^ y) ^ nComponents) / ( (double)0x100000000ULL );
#                 endif
                }

                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);

                // process the pixel (the actual computation goes here)
                switch (_dither) {
                case eDitherNone: {
                    // no dithering (identical tu Nuke's Posterize)
                    for (int c = 0; c < 4; ++c) {
                        float rounded = (unpPix[c] <= 0) ? std::floor(unpPix[c] * _colors) : std::ceil(unpPix[c] * _colors - 1.);
                        //tmpPix[c] = std::floor(unpPix[c] * _colors) / (_colors - 1.); // ok except when unpPix[c] * _colors is a positive integer
                        tmpPix[c] = rounded / (_colors - 1.);
                    }
                    break;
                }
                case eDitherOrderedBayer2: {
                    // 2x2 Bayer
#undef MSIZE
#define MSIZE 2
                    int subx = x % MSIZE;
                    if (subx < 0) {
                        subx += MSIZE;
                    }
                    int suby = y % MSIZE;
                    if (suby < 0) {
                        suby += MSIZE;
                    }
                    int dith = bayer2[subx][suby];
                    for (int c = 0; c < 4; ++c) {
                        float v = unpPix[c] * (_colors - 1.) + 1. / (2 * MSIZE * MSIZE); // ok for integer _colors
                        float fv = std::floor(v);
                        if ( (v - fv) * (MSIZE * MSIZE) <= (dith + 1) ) {
                            tmpPix[c] = fv / (_colors - 1.);
                        } else {
                            tmpPix[c] = (fv + 1) / (_colors - 1.);
                        }
                    }
                    break;
                }
                case eDitherOrderedBayer4: {
                    // 4x4 Bayer
#undef MSIZE
#define MSIZE 4
                    int subx = x % MSIZE;
                    if (subx < 0) {
                        subx += MSIZE;
                    }
                    int suby = y % MSIZE;
                    if (suby < 0) {
                        suby += MSIZE;
                    }
                    int dith = bayer4[subx][suby];
                    for (int c = 0; c < 4; ++c) {
                        float v = unpPix[c] * (_colors - 1.) + 1. / (2 * MSIZE * MSIZE); // ok for integer _colors
                        float fv = std::floor(v);
                        if ( (v - fv) * (MSIZE * MSIZE) <= (dith + 1) ) {
                            tmpPix[c] = fv / (_colors - 1.);
                        } else {
                            tmpPix[c] = (fv + 1) / (_colors - 1.);
                        }
                    }
                    break;
                }
                case eDitherOrderedBayer8: {
                    // 8x8 Bayer
#undef MSIZE
#define MSIZE 8
                    int subx = x % MSIZE;
                    if (subx < 0) {
                        subx += MSIZE;
                    }
                    int suby = y % MSIZE;
                    if (suby < 0) {
                        suby += MSIZE;
                    }
                    int dith = bayer8[subx][suby];
                    for (int c = 0; c < 4; ++c) {
                        float v = unpPix[c] * (_colors - 1.) + 1. / (2 * MSIZE * MSIZE); // ok for integer _colors
                        float fv = std::floor(v);
                        if ( (v - fv) * (MSIZE * MSIZE) <= (dith + 1) ) {
                            tmpPix[c] = fv / (_colors - 1.);
                        } else {
                            tmpPix[c] = (fv + 1) / (_colors - 1.);
                        }
                    }
                    break;
                }
                case eDitherOrderedVAC14: {
                    // 14x14 void-and-cluster
#undef MSIZE
#define MSIZE 14
                    int subx = x % MSIZE;
                    if (subx < 0) {
                        subx += MSIZE;
                    }
                    int suby = y % MSIZE;
                    if (suby < 0) {
                        suby += MSIZE;
                    }
                    int dith = vac14[subx][suby];
                    for (int c = 0; c < 4; ++c) {
                        float v = unpPix[c] * (_colors - 1.) + 1. / (2 * MSIZE * MSIZE); // ok for integer _colors
                        float fv = std::floor(v);
                        if ( (v - fv) * (MSIZE * MSIZE) <= (dith + 1) ) {
                            tmpPix[c] = fv / (_colors - 1.);
                        } else {
                            tmpPix[c] = (fv + 1) / (_colors - 1.);
                        }
                    }
                    break;
                }
                case eDitherOrderedVAC25: {
                    // 25x25 void-and-cluster
#undef MSIZE
#define MSIZE 25
                    int subx = x % MSIZE;
                    if (subx < 0) {
                        subx += MSIZE;
                    }
                    int suby = y % MSIZE;
                    if (suby < 0) {
                        suby += MSIZE;
                    }
                    int dith = vac25[subx][suby];
                    for (int c = 0; c < 4; ++c) {
                        float v = unpPix[c] * (_colors - 1.) + 1. / (2 * MSIZE * MSIZE); // ok for integer _colors
                        float fv = std::floor(v);
                        if ( (v - fv) * (MSIZE * MSIZE) <= (dith + 1) ) {
                            tmpPix[c] = fv / (_colors - 1.);
                        } else {
                            tmpPix[c] = (fv + 1) / (_colors - 1.);
                        }
                    }
                    break;
                }
                case eDitherRandom: {
                    for (int c = 0; c < 4; ++c) {
#                         ifdef USE_RANDOMGENERATOR
                        randValue = randy.random();
#                         else
                        randValue = hash(hash(hash(_seed ^ x) ^ y) ^ c) / ( (double)0x100000000ULL );
#                         endif

                        float rounded = (unpPix[c] <= 0) ? std::floor(unpPix[c] * _colors) : std::ceil(unpPix[c] * _colors - 1.);
                        float v = unpPix[c] * (_colors - 1.);     // ok for integer _colors
                        float fv = (rounded <= v) ? rounded : (rounded - 1.);
                        assert( (v - fv) >= 0 );
                        assert( (v - fv) < 1 );
                        if ( (v - fv) <= randValue ) {
                            tmpPix[c] = fv / (_colors - 1.);
                        } else {
                            tmpPix[c] = (fv + 1) / (_colors - 1.);
                        }
                    }
                    break;
                }
                } // switch
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // copy back original values from unprocessed channels
                if (nComponents == 1) {
                    if (!processA) {
                        dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                } else if ( (nComponents == 3) || (nComponents == 4) ) {
                    if (!processR) {
                        dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                    if (!processG) {
                        dstPix[1] = srcPix ? srcPix[1] : PIX();
                    }
                    if (!processB) {
                        dstPix[2] = srcPix ? srcPix[2] : PIX();
                    }
                    if ( !processA && (nComponents == 4) ) {
                        dstPix[3] = srcPix ? srcPix[3] : PIX();
                    }
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // process
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class QuantizePlugin
    : public ImageEffect
{
public:

    /** @brief ctor */
    QuantizePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _processA(NULL)
        , _colors(NULL)
        , _premult(NULL)
        , _premultChannel(NULL)
        , _mix(NULL)
        , _maskApply(NULL)
        , _maskInvert(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        // TODO: fetch noise parameters

        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);

        _colors = fetchDoubleParam(kParamColors);
        _dither = fetchChoiceParam(kParamDither);
        _seed   = fetchIntParam(kParamSeed);
        _staticSeed = fetchBooleanParam(kParamStaticSeed);
        assert(_colors && _dither && _seed && _staticSeed);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template<int nComponents>
    void renderForComponents(const RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const RenderArguments &args);

    /* set up and run a processor */
    void setupAndProcess(QuantizeProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    /* Override the clip preferences, we need to say we are setting the frame varying flag */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    DoubleParam* _colors;
    ChoiceParam* _dither;
    IntParam* _seed;
    BooleanParam* _staticSeed;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
QuantizePlugin::setupAndProcess(QuantizeProcessorBase &processor,
                                const RenderArguments &args)
{
    const double time = args.time;

    auto_ptr<Image> dst( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
# ifndef NDEBUG
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
# endif
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(time) : 0 );
# ifndef NDEBUG
    if ( src.get() ) {
        checkBadRenderScaleOrField(src, args);
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
# endif
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    if ( mask.get() ) {
        checkBadRenderScaleOrField(mask, args);
    }
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    bool premult;
    int premultChannel;
    _premult->getValueAtTime(time, premult);
    _premultChannel->getValueAtTime(time, premultChannel);
    double mix;
    _mix->getValueAtTime(time, mix);

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);

    double colors = _colors->getValueAtTime(time);
    DitherEnum dither = (DitherEnum)_dither->getValueAtTime(time);
    bool staticSeed = _staticSeed->getValueAtTime(time);
    uint32_t seed = hash( (unsigned int)_seed->getValueAtTime(time) );
    if (!staticSeed) {
        float time_f = args.time;

        // set the seed based on the current time, and double it we get difference seeds on different fields
        seed = hash( *( (uint32_t*)&time_f ) ^ seed );
    }

    processor.setValues(premult, premultChannel, mix,
                        processR, processG, processB, processA, colors, dither, seed);
    processor.process();
} // QuantizePlugin::setupAndProcess

// the overridden render function
void
QuantizePlugin::render(const RenderArguments &args)
{
    //std::cout << "render!\n";
    // instantiate the render code based on the pixel depth of the dst clip
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(OFX_COMPONENTS_OK(dstComponents));
    // do the rendering
    switch (dstComponents) {
    case ePixelComponentRGBA:
        renderForComponents<4>(args);
        break;
    case ePixelComponentRGB:
        renderForComponents<3>(args);
        break;
#ifdef OFX_EXTENSIONS_NATRON
    case ePixelComponentXY:
        renderForComponents<2>(args);
        break;
#endif
    case ePixelComponentAlpha:
        renderForComponents<1>(args);
        break;
    default:
        //std::cout << "components usupported\n";
        throwSuiteStatusException(kOfxStatErrUnsupported);
        break;
    } // switch
      //std::cout << "render! OK\n";
}

template<int nComponents>
void
QuantizePlugin::renderForComponents(const RenderArguments &args)
{
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
    case eBitDepthUByte:
        renderForBitDepth<unsigned char, nComponents, 255>(args);
        break;

    case eBitDepthUShort:
        renderForBitDepth<unsigned short, nComponents, 65535>(args);
        break;

    case eBitDepthFloat:
        renderForBitDepth<float, nComponents, 1>(args);
        break;
    default:
        //std::cout << "depth usupported\n";
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
QuantizePlugin::renderForBitDepth(const RenderArguments &args)
{
    QuantizeProcessor<PIX, nComponents, maxValue> fred(*this, args);
    setupAndProcess(fred, args);
}

bool
QuantizePlugin::isIdentity(const IsIdentityArguments &args,
                           Clip * &identityClip,
                           double & /*identityTime*/
                           , int& /*view*/, std::string& /*plane*/)
{
    //std::cout << "isIdentity!\n";
    const double time = args.time;
    double mix;

    _mix->getValueAtTime(time, mix);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;

        return true;
    }

    {
        bool processR;
        bool processG;
        bool processB;
        bool processA;
        _processR->getValueAtTime(time, processR);
        _processG->getValueAtTime(time, processG);
        _processB->getValueAtTime(time, processB);
        _processA->getValueAtTime(time, processA);
        if (!processR && !processG && !processB && !processA) {
            identityClip = _srcClip;

            return true;
        }
    }

    // TODO: which plugin parameter values give identity?
    //if (...) {
    //    identityClip = _srcClip;
    //    //std::cout << "isIdentity! true\n";
    //    return true;
    //}

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                identityClip = _srcClip;

                return true;
            }
        }
    }

    //std::cout << "isIdentity! false\n";
    return false;
} // QuantizePlugin::isIdentity

void
QuantizePlugin::changedClip(const InstanceChangedArgs &args,
                            const std::string &clipName)
{
    //std::cout << "changedClip!\n";
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) && _srcClip && (args.reason == eChangeUserEdit) ) {
        switch ( _srcClip->getPreMultiplication() ) {
        case eImageOpaque:
            _premult->setValue(false);
            break;
        case eImagePreMultiplied:
            _premult->setValue(true);
            break;
        case eImageUnPreMultiplied:
            _premult->setValue(false);
            break;
        }
    }
    //std::cout << "changedClip OK!\n";
}

/* Override the clip preferences, we need to say we are setting the frame varying flag */
void
QuantizePlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    DitherEnum dither = (DitherEnum)_dither->getValue();

    if (dither == eDitherRandom) {
        bool staticSeed = _staticSeed->getValue();
        if (!staticSeed) {
            clipPreferences.setOutputFrameVarying(true);
            clipPreferences.setOutputHasContinuousSamples(true);
        }
    }
}

mDeclarePluginFactory(QuantizePluginFactory, {ofxsThreadSuiteCheck();}, {});
void
QuantizePluginFactory::describe(ImageEffectDescriptor &desc)
{
    //std::cout << "describe!\n";
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

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
    //std::cout << "describe! OK\n";
}

void
QuantizePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                         ContextEnum context)
{
    //std::cout << "describeInContext!\n";
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
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }


    // describe plugin params
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamColors);
        param->setLabel(kParamColorsLabel);
        param->setHint(kParamColorsHint);
        param->setRange(0, DBL_MAX);
        param->setDisplayRange(kParamColorsMin, kParamColorsMax);
        param->setDefault(kParamColorsDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamDither);
        param->setLabel(kParamDitherLabel);
        param->setHint(kParamDitherHint);
        param->setAnimates(false);
        assert(param->getNOptions() == eDitherNone);
        param->appendOption(kParamDitherOptionNone);
        assert(param->getNOptions() == eDitherOrderedBayer2);
        param->appendOption(kParamDitherOptionOrderedBayer2);
        assert(param->getNOptions() == eDitherOrderedBayer4);
        param->appendOption(kParamDitherOptionOrderedBayer4);
        assert(param->getNOptions() == eDitherOrderedBayer8);
        param->appendOption(kParamDitherOptionOrderedBayer8);
        assert(param->getNOptions() == eDitherOrderedVAC14);
        param->appendOption(kParamDitherOptionOrderedVoidAndCluster14);
        assert(param->getNOptions() == eDitherOrderedVAC25);
        param->appendOption(kParamDitherOptionOrderedVoidAndCluster25);
        assert(param->getNOptions() == eDitherRandom);
        param->appendOption(kParamDitherOptionRandom);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
    // seed
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamSeed);
        param->setLabel(kParamSeedLabel);
        param->setHint(kParamSeedHint);
        param->setDefault(2000);
        param->setAnimates(true); // can animate
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamStaticSeed);
        param->setLabel(kParamStaticSeedLabel);
        param->setHint(kParamStaticSeedHint);
        param->setDefault(false);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
    //std::cout << "describeInContext! OK\n";
} // QuantizePluginFactory::describeInContext

ImageEffect*
QuantizePluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    return new QuantizePlugin(handle);
}

static QuantizePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
