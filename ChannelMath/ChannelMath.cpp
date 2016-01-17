/* 
=====================================================================================
LICENSE
=========================================================================================

Copyright (C) 2015 Nicholas Carroll
http://casanico.com

ChannelMath is free software: you can redistribute it and/or modify it under the terms of the GNU 
General Public License as published by the Free Software Foundation; either version 3 of 
the License, or (at your option) any later version.

ChannelMath is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without 
even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See 
the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with ChannelMath. If not,
see http://www.gnu.org/licenses/gpl-3.0.html

The libraries used by this program come from:

1. F. Deverney's openfx-misc:
* https://github.com/devernay/openfx-misc
licensed under GNU GPL 2.0: http://www.gnu.org/licenses/gpl-2.0.html

2. Arash Partow's C++ mathematical expression library:
* http://www.partow.net/programming/exprtk
licensed under CPL 1.0: http://opensource.org/licenses/cpl1.0.php

=====================================================================================
VERSION HISTORY
=====================================================================================

Version   Date       Author       Description
-------   ---------  ----------   ---------------------------------------------------
    1.0   14-NOV-15  N. Carroll   First version

* TODO refactor to make it faster
* TODO Find and fix the source of the NaN errors that sometimes occur
 */

#include "ChannelMath.h"
#include <cstring>
#include <cmath>

#ifdef _WINDOWS
#include <windows.h>
#endif


#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "exprtk.hpp"

using namespace OFX;
using namespace std;

#define kPluginName "ChannelMath"
#define kPluginGrouping "Color/Math"
#define kPluginDescription \
"RGBA channel math expression node.\n" \
"Copyleft 2015 Nicholas Carroll\n" \
"http://casanico.com \n\n" \
"SYMBOLS REFERENCE:  \n" \
"* r,g,b,a : color channel red, green, blue, alpha  \n" \
"* x,y: pixel coordinate \n" \
"* +, -, *, /, ^, % : Math operators    \n" \
"* =   : Assignment operator  \n" \
"* fmod  : Modulus. Same as %  \n" \
"* min(a,b,...) : Min of any number of variables \n" \
"* max, avg, sum   \n" \
"* abs(a) : Absolute value   \n" \
"* ceil, floor, round: Nearest integer up/down \n" \
"* pow(a,b) : a to the power of b. Same as ^ \n" \
"* exp, log, root, sqrt.  \n" \
"* if(a == b, c, d) : If a equals b then c, else d.\n" \
"* a==b?c:d : If a equals b then c, else d.\n" \
"* ==, !=, <, >, >=, <= : Conditionals.\n" \
"* sin, cos, tan, asin, acos, atan.  \n" \
"* atan2(a,b) : Arc tangent of a and b.  \n" \
"* hypot(a,b) : Hypotenuse.  \n" \
"* pi : 3.141592653589793238462    \n" \
"* clamp(a,b,c) : a clamped to between b and c \n" \
"* lerp(a,b,c) : Linear interpolation of a between b and c   \n"	\
"   The formula used by lerp is a*(c-b)+b"			

#define kPluginIdentifier "com.casanico.ChannelMath"
#define kPluginVersionMajor 1 
#define kPluginVersionMinor 0 

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths true
#define kRenderThreadSafety eRenderFullySafe

static const string  kParamExpr1Name = "expr1";
static const string  kParamExpr1Label= "expr1";
static const string  kParamExpr1Hint = "You can define an expression here and reference it in ChannelMath fields as 'expr1'";
static const string  kParamExpr2Name = "expr2";
static const string  kParamExpr2Label= "expr2";
static const string  kParamExpr2Hint = "Reference in ChannelMath fields as'expr2'";


static const string  kParamChannelMathR =     "red";
static const string  kParamChannelMathRLabel= "red";
static const string  kParamChannelMathRHint = "Red channel output";
static const string  kParamChannelMathG     = "green";
static const string  kParamChannelMathGLabel= "green";
static const string  kParamChannelMathGHint = "Green channel output";
static const string  kParamChannelMathB     = "blue";
static const string  kParamChannelMathBLabel ="blue";
static const string  kParamChannelMathBHint = "Blue channel output";
static const string  kParamChannelMathA     = "alpha";
static const string  kParamChannelMathALabel= "alpha";
static const string  kParamChannelMathAHint = "Alpha channel output";

static const string  kParamParam1Name = "param1";
static const string  kParamParam1Label= "param1";
static const string  kParamParam1Hint = "Reference in ChannelMath fields as 'param1'";

static const string  kParamParam2Name = "param2";
static const string  kParamParam2Label= "param2";
static const string  kParamParam2Hint = "Reference in ChannelMath fields as 'param2'";

static const string  kParamStatusName = "status";
static const string  kParamStatusLabel= "Status";
static const string kParamStatusHint = "Write validation information across the viewer";

namespace {
    struct RGBAValues {
        double r,g,b,a;
        RGBAValues(double v) : r(v), g(v), b(v), a(v) {}
        RGBAValues() : r(0), g(0), b(0), a(0) {}
    };
}

struct ChannelMathProperties {
  const string name;
  string content;
  bool processFlag;
};

namespace {
string replace_pattern(string text, const string& pattern,
			    const string& replacement) {
     size_t pos = 0;
    while ((pos = text.find(pattern,pos)) != string::npos){
      text.replace(pos,pattern.length(),replacement);
      pos += replacement.length();
    }
    return text;
}
}

class ChannelMathProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    string _expr1;
    string _expr2;
    string _exprR;
    string _exprG;
    string _exprB;
    string _exprA;
    RGBAValues _param1;
    double _param2;
    bool _premult;
    int _premultChannel;
    bool   _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;

public:
    
    ChannelMathProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance),
      _srcImg(0),
      _maskImg(0),
      _expr1(),
      _expr2(),
      _exprR(),
      _exprG(),
      _exprB(),
      _exprA(),
      _param1(),
      _param2(),
      _premult(false),
      _premultChannel(3),
      _doMasking(false),
      _mix(1.),
      _maskInvert(false),
      _processR(true),
      _processG(true),
      _processB(true),
      _processA(true)
    {
    }
    
    void setSrcImg(const OFX::Image *v) {_srcImg = v;}
    
    void setMaskImg(const OFX::Image *v, bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }
    
    void doMasking(bool v) {_doMasking = v;}
    
  void setValues( const string& expr1,
		  const string& expr2,
		  const string& exprR,
		  const string& exprG,
		  const string& exprB,
		  const string& exprA,
		 const RGBAValues& param1,
		  double param2,
        bool premult,
        int premultChannel,
        double mix,
        bool processR,
        bool processG,
        bool processB,
        bool processA
                 )
    {
      _expr1 = expr1;
      _expr2 = expr2;
      _exprR = exprR;
      _exprG = exprG;
      _exprB = exprB;
      _exprA = exprA;
        _param1 = param1;
	_param2 = param2;
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
    }

private:
};



template <class PIX, int nComponents, int maxValue>
class ChannelMathProcessor : public ChannelMathProcessorBase
{
public:
    ChannelMathProcessor(OFX::ImageEffect &instance)
    : ChannelMathProcessorBase(instance)
    {
    }
    
private:

    void multiThreadProcessImages(OfxRectI procWindow)
  {  
    
    int chans = ((_processR ? 0xf000 : 0) | (_processG ? 0x0f00 : 0) | (_processB ? 0x00f0 : 0) | (_processA ? 0x000f : 0));
    if (nComponents == 1) {
            switch (chans) {
                case 0x0000:
                case 0x00f0:
                case 0x0f00:
                case 0x0ff0:
                case 0xf000:
                case 0xf0f0:
                case 0xff00:
                case 0xfff0:
                    return process<false,false,false,false>(procWindow);
                case 0x000f:
                case 0x00ff:
                case 0x0f0f:
                case 0x0fff:
                case 0xf00f:
                case 0xf0ff:
                case 0xff0f:
                case 0xffff:
                    return process<false,false,false,true >(procWindow);
            }
    } else if (nComponents == 3) {
            switch (chans) {
                case 0x0000:
                case 0x000f:
                    return process<false,false,false,false>(procWindow);
                case 0x00f0:
                case 0x00ff:
                    return process<false,false,true ,false>(procWindow);
                case 0x0f00:
                case 0x0f0f:
                    return process<false,true ,false,false>(procWindow);
                case 0x0ff0:
                case 0x0fff:
                    return process<false,true ,true ,false>(procWindow);
                case 0xf000:
                case 0xf00f:
                    return process<true ,false,false,false>(procWindow);
                case 0xf0f0:
                case 0xf0ff:
                    return process<true ,false,true ,false>(procWindow);
                case 0xff00:
                case 0xff0f:
                    return process<true ,true ,false,false>(procWindow);
                case 0xfff0:
                case 0xffff:
                    return process<true ,true ,true ,false>(procWindow);
            }
    } else if (nComponents == 4) {
            switch (chans) {
                case 0x0000:
                    return process<false,false,false,false>(procWindow);
                case 0x000f:
                    return process<false,false,false,true >(procWindow);
                case 0x00f0:
                    return process<false,false,true ,false>(procWindow);
                case 0x00ff:
                    return process<false,false,true, true >(procWindow);
                case 0x0f00:
                    return process<false,true ,false,false>(procWindow);
                case 0x0f0f:
                    return process<false,true ,false,true >(procWindow);
                case 0x0ff0:
                    return process<false,true ,true ,false>(procWindow);
                case 0x0fff:
                    return process<false,true ,true ,true >(procWindow);
                case 0xf000:
                    return process<true ,false,false,false>(procWindow);
                case 0xf00f:
                    return process<true ,false,false,true >(procWindow);
                case 0xf0f0:
                    return process<true ,false,true ,false>(procWindow);
                case 0xf0ff:
                    return process<true ,false,true, true >(procWindow);
                case 0xff00:
                    return process<true ,true ,false,false>(procWindow);
                case 0xff0f:
                    return process<true ,true ,false,true >(procWindow);
                case 0xfff0:
                    return process<true ,true ,true ,false>(procWindow);
                case 0xffff:
                    return process<true ,true ,true ,true >(procWindow);
            }
        }
  }

  template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow)
  {
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float unpPix[4];
        float tmpPix[4];

	bool doR = processR;
	bool doG = processG;
	bool doB = processB;
	bool doA = processA;

	//SYMBOLS FOR EXPRESSIONS
	float param1_red;
	float param1_green;
	float param1_blue;
	float param1_alpha;
	float param2;
	float x_coord;
	float y_coord;

	exprtk::symbol_table<float> symbol_table;
	symbol_table.add_constants();
	symbol_table.add_variable("r",unpPix[0]);
	symbol_table.add_variable("g",unpPix[1]);
	symbol_table.add_variable("b",unpPix[2]);
	symbol_table.add_variable("a",unpPix[3]);
	symbol_table.add_variable("param1_r",param1_red);
	symbol_table.add_variable("param1_g",param1_green);
	symbol_table.add_variable("param1_b",param1_blue);
	symbol_table.add_variable("param1_a",param1_alpha);
	symbol_table.add_variable("param2", param2);
    symbol_table.add_variable("x",x_coord);
    symbol_table.add_variable("y",y_coord);
	
	ChannelMathProperties expr1_props = {kParamExpr1Name, _expr1, true};
	ChannelMathProperties expr2_props = {kParamExpr2Name, _expr2, true};
	ChannelMathProperties exprR_props = {kParamChannelMathR, _exprR, true};
	ChannelMathProperties exprG_props = {kParamChannelMathG, _exprG, true};
	ChannelMathProperties exprB_props = {kParamChannelMathB, _exprB, true};
	ChannelMathProperties exprA_props = {kParamChannelMathA, _exprA, true};

	const int Esize = 6;
	ChannelMathProperties E[Esize] = {expr1_props, expr2_props, exprR_props,
					 exprG_props, exprB_props, exprA_props};

	for (int i = 0; i != Esize; ++i) {
	  for (int k = 0; k != Esize; ++ k) {
	    //if the expression references itself it is invalid and will be deleted for its henious crime
	    if (E[i].content.find(E[i].name) != string::npos){
	      E[i].content.clear();
	      E[i].processFlag = false;
	    }  //otherwise away we go and break down refs to all the other expressions 
	    else if ((i != k) && !E[i].content.empty() && !E[k].content.empty() ) { 
            E[i].content  = replace_pattern(E[i].content,E[k].name,"("+E[k].content+")");
	    }
	  }
	//exprtk does not like dot based naming so use underscores
	E[i].content = replace_pattern(E[i].content,"param1.","param1_");
	E[i].content = replace_pattern(E[i].content,"param2.","param2_");
	E[i].content = replace_pattern(E[i].content,"=",":=");
    E[i].content = replace_pattern(E[i].content,":=:=","==");
	}
      
    //define custom functions for exprtk to match SeExpr
    exprtk::function_compositor<float> compositor(symbol_table);
      // define function lerp(a,b,c) {a*(c-b)+b}
      compositor
      .add("lerp",
           " a*(c-b)+b;",
           "a","b","c");
      //clamp could not be overloaded so I've modified the exprtk.hpp for that
      
	// load expression for exprR
	exprtk::expression<float> expressionR;
	expressionR.register_symbol_table(symbol_table);
	exprtk::parser<float> parserR;
	doR = parserR.compile(E[2].content,expressionR);
	// load expression for exprG
	exprtk::expression<float> expressionG;
	expressionG.register_symbol_table(symbol_table);
	exprtk::parser<float> parserG;
        doG = parserG.compile(E[3].content,expressionG);
	// load expression for exprB
	exprtk::expression<float> expressionB;
	expressionB.register_symbol_table(symbol_table);
	exprtk::parser<float> parserB;
	doB = parserB.compile(E[4].content,expressionB);
	// load expression for exprA
      	exprtk::expression<float> expressionA;
	expressionA.register_symbol_table(symbol_table);
	exprtk::parser<float> parserA;
	doA = parserA.compile(E[5].content,expressionA);
	
	// pixelwise
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
        
                //for the symbol table
                param1_red = (float)_param1.r;
                param1_green = (float)_param1.g;
                param1_blue = (float)_param1.b;
                param1_alpha = (float)_param1.a;
                param2 = (float)_param2;
                x_coord = x;
                y_coord = y;
                
                //we take all the valid expressions and concatenate them in order with ;
                //and get a vector result

                // UPDATE ALL THE PIXELS
                for (int c = 0; c < 4; ++c) {
                    if (doR && c == 0) {
                        // RED OUTPUT CHANNEL
                        tmpPix[0] = expressionR.value();
                    } else if (doG && c == 1) {
                        // GREEN OUTPUT CHANNEL
                        tmpPix[1] = expressionG.value();
                    } else if (doB && c == 2) {
                        // BLUE OUTPUT CHANNEL
                        tmpPix[2] = expressionB.value();
                    } else if (doA && c == 3) {
                        // ALPHA OUTPUT CHANNEL
                        tmpPix[3] = expressionA.value();
                    } else {
                        tmpPix[c] = unpPix[c];
                    }
                }
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix,
							      	_doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // copy back original values from unprocessed channels
                if (nComponents == 1) {
                    if (!doA) {
                        dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                } else if (nComponents == 3 || nComponents == 4) {
                    if (!doR) {
                        dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                    if (!doG) {
                        dstPix[1] = srcPix ? srcPix[1] : PIX();
                    }
                    if (!doB) {
                        dstPix[2] = srcPix ? srcPix[2] : PIX();
                    }
                    if (!doA && nComponents == 4) {
                        dstPix[3] = srcPix ? srcPix[3] : PIX();
                    }
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};


class ChannelMathPlugin : public OFX::ImageEffect
{
public:
    ChannelMathPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _maskClip(0)
  {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB ||
			    _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB ||
			    _srcClip->getPixelComponents() == ePixelComponentRGBA));
        _maskClip = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);

	_expr1 = fetchStringParam(kParamExpr1Name);
	_expr2 = fetchStringParam(kParamExpr2Name);
        _exprR = fetchStringParam(kParamChannelMathR);
        _exprG = fetchStringParam(kParamChannelMathG);
        _exprB = fetchStringParam(kParamChannelMathB);
        _exprA = fetchStringParam(kParamChannelMathA);
        assert(_expr1 && _expr2 && _exprR && _exprG && _exprB && _exprA);
	
        _param1 = fetchRGBAParam(kParamParam1Name);
        assert(_param1);
	_param2 = fetchDoubleParam(kParamParam2Name);
	assert(_param2);
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
  }
    
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    /* set up and run a processor */
    void setupAndProcess(ChannelMathProcessorBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const string &clipName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;
    OFX::StringParam* _expr1;
    OFX::StringParam* _expr2;
    OFX::StringParam* _exprR;
    OFX::StringParam* _exprG;
    OFX::StringParam* _exprB;
    OFX::StringParam* _exprA;
    OFX::RGBAParam *_param1;
    OFX::DoubleParam* _param2;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};



/* set up and run a processor */
void
ChannelMathPlugin::setupAndProcess(ChannelMathProcessorBase &processor, const OFX::RenderArguments &args)
{
    auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(args.time) : 0);
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    auto_ptr<const OFX::Image> mask((getContext() != OFX::eContextFilter && _maskClip && _maskClip->isConnected()) ?
                                         _maskClip->fetchImage(args.time) : 0);
    // do we do masking
    if (getContext() != OFX::eContextFilter && _maskClip && _maskClip->isConnected()) {
        if (mask.get()) {
            if (mask->getRenderScale().x != args.renderScale.x ||
                mask->getRenderScale().y != args.renderScale.y ||
                (mask->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && mask->getField() != args.fieldToRender)) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
        }
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }
    
    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    // set the render window
    processor.setRenderWindow(args.renderWindow);
    string expr1;
    string expr2;
    string exprR;
    string exprG;
    string exprB;
    string exprA;
    _expr1->getValue(expr1);
    _expr2->getValue(expr2);
    _exprR->getValue(exprR);
    _exprG->getValue(exprG);
    _exprB->getValue(exprB);
    _exprA->getValue(exprA);
    RGBAValues param1;
    _param1->getValueAtTime(args.time, param1.r, param1.g, param1.b, param1.a);
    double param2;
    _param2->getValueAtTime(args.time,param2);
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    
    //we wont process any channel that has a null expression
    //we won't worry about invalid expressions
    bool processR = !exprR.empty();
    bool processG = !exprG.empty();
    bool processB = !exprB.empty();
    bool processA = !exprA.empty();
    
    processor.setValues(expr1, expr2, exprR, exprG, exprB, exprA,
                        param1, param2, premult, premultChannel, mix, processR, processG,
                        processB, processA);
 
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
void
ChannelMathPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    
    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    assert(dstComponents == OFX::ePixelComponentAlpha || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                ChannelMathProcessor<unsigned char, 4, 255> pierre(*this);
                setupAndProcess(pierre, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                ChannelMathProcessor<unsigned short, 4, 65535> pierre(*this);
                setupAndProcess(pierre, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                ChannelMathProcessor<float, 4, 1> pierre(*this);
                setupAndProcess(pierre, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else if (dstComponents == OFX::ePixelComponentAlpha) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                ChannelMathProcessor<unsigned char, 1, 255> pierre(*this);
                setupAndProcess(pierre, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                ChannelMathProcessor<unsigned short, 1, 65535> pierre(*this);
                setupAndProcess(pierre, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                ChannelMathProcessor<float, 1, 1> pierre(*this);
                setupAndProcess(pierre, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                ChannelMathProcessor<unsigned char, 3, 255> pierre(*this);
                setupAndProcess(pierre, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                ChannelMathProcessor<unsigned short, 3, 65535> pierre(*this);
                setupAndProcess(pierre, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                ChannelMathProcessor<float, 3, 1> pierre(*this);
                setupAndProcess(pierre, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


bool
ChannelMathPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0.) {
        identityClip = _srcClip;
        return true;
    }

    string expr1, expr2, exprR,  exprG,  exprB,  exprA;
    _expr1->getValue(expr1);
    _expr2->getValue(expr2);
    _exprR->getValue(exprR);
    _exprG->getValue(exprG);
    _exprB->getValue(exprB);
    _exprA->getValue(exprA);
    RGBAValues param1;
    _param1->getValueAtTime(args.time, param1.r, param1.g, param1.b, param1.a);
    if (exprR.empty() && exprG.empty() && exprB.empty() && exprA.empty()) {
        identityClip = _srcClip;
        return true;
    }
    
    return false;
}

void
ChannelMathPlugin::changedClip(const InstanceChangedArgs &args, const string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && _srcClip && args.reason == OFX::eChangeUserEdit) {
        switch (_srcClip->getPreMultiplication()) {
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
}


mDeclarePluginFactory(ChannelMathPluginFactory, {}, {});

void ChannelMathPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    if (gHostDescription.hostName == "uk.co.thefoundry.nuke") {
        desc.setLabel("OFX"kPluginName);
    } else {
        desc.setLabel(kPluginName);
    }
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
}

void ChannelMathPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);
    
    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral)
            maskClip->setOptional(true);
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(true);
    }
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamExpr1Name);
        param->setLabel(kParamExpr1Label);
        param->setHint(kParamExpr1Hint);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamExpr2Name);
        param->setLabel(kParamExpr2Label);
        param->setHint(kParamExpr2Hint);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamChannelMathR);
        param->setLabel(kParamChannelMathRLabel);
        param->setHint(kParamChannelMathRHint);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamChannelMathG);
        param->setLabel(kParamChannelMathGLabel);
        param->setHint(kParamChannelMathGHint);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamChannelMathB);
        param->setLabel(kParamChannelMathBLabel);
        param->setHint(kParamChannelMathBHint);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamChannelMathA);
        param->setLabel(kParamChannelMathALabel);
        param->setHint(kParamChannelMathAHint);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    

    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamParam1Name);
        param->setLabel(kParamParam1Label);
        param->setHint(kParamParam1Hint);
        param->setDefault(1.0, 1.0, 1.0, 1.0);
        param->setDisplayRange(0, 0, 0, 0, 4, 4, 4, 4);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamParam2Name);
        param->setLabel(kParamParam2Label);
        param->setHint(kParamParam2Hint);
        param->setDefault(1.0);
        param->setDisplayRange(-100., 100.);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

//     {
//         BooleanParamDescriptor *param = desc.defineBooleanParam(kParamStatusName);
//         param->setLabel(kParamStatusLabel);
//         param->setHint(kParamStatusHint);
//         param->setDefault(false);
//         param->setLayoutHint(eLayoutHintNoNewLine);
//         param->setAnimates(false);
//         if (page) {
//             page->addChild(*param);
//         }
//     }

    
    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect* ChannelMathPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new ChannelMathPlugin(handle);
}

void getChannelMathPluginID(OFX::PluginFactoryArray &ids)
{
    static ChannelMathPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

