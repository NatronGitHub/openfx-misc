/*
 OFX CImgExpression plugin.

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

#include "CImgExpression.h"

#include <memory>
#include <cmath>
#include <cstring>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsMerging.h"
#include "ofxsCopier.h"

#include "CImgFilter.h"

#define kPluginName          "GMICExpr"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Quickly generate image from mathematical formula evaluated for each pixel of the selected images.\n"\
"Full documentation for G'MIC/CImg expressions can be found at http://gmic.eu/reference.shtml#section9\n"\
"The only difference is the 't' variable, which is defined to current time by default.\n"\
"  - The mathematical parser understands the following set of functions, operators and variables:\n"\
"    _ Usual operators: || (logical or), && (logical and), | (bitwise or), & (bitwise and), !=, ==, <=, >=, <, >, << (left bitwise shift), >> (right bitwise shift), -, +, *, /, % (modulo), ^ (power), ! (logical not), ~ (bitwise not).\n"\
"    _ Usual functions: sin(), cos(), tan(), asin(), acos(), atan(), sinh(), cosh(), tanh(),log(), log2(), log10(), exp(), sign(), abs(), atan2(), round(), narg(), arg(), isval(), isnan(), isinf(), isint(), isbool(), isdir(), isfile(), rol() (left bit rotation), ror() (right bit rotation), min(), max(), med(), kth(), sinc(), int().\n"\
"       Function 'atan2()' is the version of 'atan()' with two arguments 'y' and 'x' (as in C/C++).\n"\
"       Function 'narg()' returns the number of specified arguments.\n"\
"       Function 'arg(i,a_1,..,a_n)' returns the ith argument a_i.\n"\
"       Functions 'min()', 'max()', 'med()' and 'kth()' can be called with an arbitrary number of arguments.\n"\
"       Functions 'isval()', 'isnan()', 'isinf()', 'isbool()' can be used to test the type of a given number or expression.\n"\
"       Function 'isfile()' (resp. 'isdir()') returns 0 (false) or 1 (true) whether its argument is a valid path to a file (resp. to a directory) or not.\n"\
"       Function 'fdate(path,attr)' returns the date attribute for the given 'path' (file or directory), with 'attr' being { 0=year | 1=month | 2=day | 3=day of week | 4=hour | 5=minute | 6=second }.\n"\
"       Function 'isin(v,a_1,...,a_n)' returns 0 (false) or 1 (true) whether the first value 'v' appears in the set of other values 'a_i'.\n"\
"    _ Variable names below are pre-defined. They can be overloaded.\n"\
"         . 'w': width of the associated image, if any (0 otherwise).\n"\
"         . 'h': height of the associated image, if any (0 otherwise).\n"\
"         . 'd': depth of the associated image, if any (0 otherwise).\n"\
"         . 's': spectrum of the associated image, if any (0 otherwise).\n"\
"         . 'x': current processed column of the associated image, if any (0 otherwise).\n"\
"         . 'y': current processed row of the associated image, if any (0 otherwise).\n"\
"         . 'z': current processed slice of the associated image, if any (0 otherwise).\n"\
"         . 'c': current processed channel of the associated image, if any (0 otherwise).\n"\
"         . 't': current time.\n"\
"         . 'e': value of e, i.e. 2.71828..\n"\
"         . 'pi': value of pi, i.e. 3.1415926..\n"\
"         . '?' or 'u': a random value between [0,1], following a uniform distribution.\n"\
"         . 'g': a random value, following a gaussian distribution of variance 1 (roughly in [-5,5]).\n"\
"         . 'i': current processed pixel value (i.e. value located at (x,y,z,c)) of the associated image, if any (0 otherwise).\n"\
"         . 'im','iM','ia','iv','ic': Respectively the minimum, maximum, average values, variance and median value of the associated image, if any (0 otherwise).\n"\
"         . 'xm','ym','zm','cm': The pixel coordinates of the minimum value in the associated image, if any (0 otherwise).\n"\
"         . 'xM','yM','zM','cM': The pixel coordinates of the maximum value in the associated image, if any (0 otherwise).\n"\
"    _ Special operators can be used:\n"\
"         . ';': expression separator. The returned value is always the last encountered expression. For instance expression '1;2;pi' is evaluated as 'pi'.\n"\
"         . '=': variable assignment. Variables in mathematical parser can only refer to numerical values. Variable names are case-sensitive. Use this operator in conjunction with ';' to define complex evaluable expressions, such as 't=cos(x);3*t^2+2*t+1'.\n"\
"            These variables remain local to the mathematical parser and cannot be accessed\n"\
"            outside the evaluated expression.\n"\
"    _ The following specific functions are also defined:\n"\
"         . 'if(expr_cond,expr_then,expr_else)': return value of 'expr_then' or 'expr_else', depending on the value of 'expr_cond' (0=false, other=true). For instance, 'if(x%10==0,255,i)' will draw blank vertical lines on every 10th column of an image.\n"\
"         . '?(max)' or '?(min,max)': return a random value between [0,max] or [min,max], following a uniform distribution. 'u(max)' and 'u(0,max)' mean the same.\n"\
"         . 'i(_a,_b,_c,_d,_interpolation,_boundary)': return the value of the pixel located at position (a,b,c,d) in the associated image, if any (0 otherwise). Interpolation parameter can be { 0=nearest neighbor | other=linear }. Boundary conditions can be { 0=dirichlet | 1=neumann | 2=periodic }. Omitted coordinates are replaced by their default values which are respectively x, y, z, c and 0.\n"\
"         . 'j(_dx,_dy,_dz,_dc,_interpolation,_boundary)': does the same for the pixel located at position (x+dx,y+dy,z+dz,c+dc).\n"\
"         . 'i[offset]': return the value of the pixel located at specified offset in the associated image buffer.\n"\
"         . 'j[offset]': does the same for an offset relative to the current pixel (x,y,z,c). For instance expression '0.5*(i(x+1)-i(x-1))' will estimate the X-derivative of an image with a classical finite difference scheme.\n"\
"         . If specified formula starts with '>' or '<', the operators 'i(..)' and 'j(..)' will return values of the image currently being modified, in forward ('>') or backward ('<') order.\n"\
"\n"\
"Sample expressions:\n"                                                 \
"'0.5*(i(x+1)-i(x-1))' will estimate the X-derivative of an image with a classical finite difference scheme.\n"\
"'if(x%10==0,255,i)' will draw blank vertical lines on every 10th column of an image.\n"\
"'X=x-w/2;Y=y-h/2;D=sqrt(Xˆ2+Yˆ2);if(D+u*20<80,abs(255*cos(D/(5+c))),10*(y%(20+c)))'\n"\
"Uses the 'fill' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgExpression"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 0 // Expression effect can only be computed on the whole image
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 0 // no render scale support
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe
#define kHostFrameThreading true
#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true

#define kParamExpression "expression"
#define kParamExpressionLabel "Expression"
#define kParamExpressionHint "G'MIC/CImg expression, see the plugin description/help, or http://gmic.eu/reference.shtml#section9"
#define kParamExpressionDefault "i(x,y,0,c)"


using namespace OFX;

/// Expression plugin
struct CImgExpressionParams
{
  std::string expr;
};

class CImgExpressionPlugin : public CImgFilterPluginHelper<CImgExpressionParams,true>
{
public:

    CImgExpressionPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgExpressionParams,true>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale)
    {
        _expr  = fetchStringParam(kParamExpression);
        assert(_expr);
    }

    virtual void getValuesAtTime(double time, CImgExpressionParams& params) OVERRIDE FINAL
    {
        _expr->getValueAtTime(time, params.expr);
    }


    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& /*renderScale*/, const CImgExpressionParams& /*params*/, OfxRectI* roi) OVERRIDE FINAL
    {
        roi->x1 = rect.x1;
        roi->x2 = rect.x2;
        roi->y1 = rect.y1;
        roi->y2 = rect.y2;
    }

    virtual void render(const OFX::RenderArguments &args, const CImgExpressionParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        char t[80];
        snprintf(t, sizeof(t), "t=%g;", args.time);
        try {
            cimg.fill((t + params.expr).c_str(), true);
        } catch (const cimg_library::CImgArgumentException& e) {
            setPersistentMessage(OFX::Message::eMessageError, "", e.what());
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &/*args*/, const CImgExpressionParams& /*params*/) OVERRIDE FINAL
    {
        // must clear persistent message in isIdentity, or render() is not called by Nuke
        clearPersistentMessage();
        return false;
    };
private:

    // params
    OFX::StringParam *_expr;
};


mDeclarePluginFactory(CImgExpressionPluginFactory, {}, {});

void CImgExpressionPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add supported context
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

    // add supported pixel depths
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(kHostFrameThreading);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}

void CImgExpressionPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgExpressionPlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles);
    {
        OFX::StringParamDescriptor *param = desc.defineStringParam(kParamExpression);
        param->setLabel(kParamExpressionLabel);
        param->setHint(kParamExpressionHint);
        param->setDefault(kParamExpressionDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgExpressionPlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgExpressionPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CImgExpressionPlugin(handle);
}


void getCImgExpressionPluginID(OFX::PluginFactoryArray &ids)
{
    static CImgExpressionPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
