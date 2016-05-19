/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2016 INRIA
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
 * OFX CImgExpression plugin.
 */

#include <memory>
#include <cmath>
#include <cstring>
#include <stdio.h> // for snprintf & _snprintf
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  include <windows.h>
#  if defined(_MSC_VER) && _MSC_VER < 1900
#    define snprintf _snprintf
#  endif
#endif // defined(_WIN32) || defined(__WIN32__) || defined(WIN32)

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsCopier.h"

#include "CImgFilter.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "GMICExpr"
#define kPluginGrouping      "Filter"
#define kPluginDescriptionUnsafe \
    "Quickly generate or process image from mathematical formula evaluated for each pixel.\n" \
    "Full documentation for G'MIC/CImg expressions can be found at http://gmic.eu/reference.shtml#section9\n" \
    "The only difference is the predefined variables 't' (current time) and 'k' (render scale).\n" \
    "\n" \
    "The mathematical parser understands the following set of functions, operators and variables:\n" \
    "    _ Usual operators: || (logical or), && (logical and), | (bitwise or), & (bitwise and),\n" \
    "       !=, ==, <=, >=, <, >, << (left bitwise shift), >> (right bitwise shift), -, +, *, /,\n" \
    "       % (modulo), ^ (power), ! (logical not), ~ (bitwise not).\n" \
    "    _ Usual functions: sin(), cos(), tan(), asin(), acos(), atan(), sinh(), cosh(), tanh(),\n" \
    "       log(), log2(), log10(), exp(), sign(), abs(), atan2(), round(), narg(), arg(),\n" \
    "       isval(), isnan(), isinf(), isint(), isbool(), isdir(), isfile(), rol() (left bit rotation),\n" \
    "       ror() (right bit rotation), min(), max(), med(), kth(), sinc(), int().\n" \
    "       . Function 'atan2()' is the version of 'atan()' with two arguments 'y' and 'x' (as in C/C++).\n" \
    "       . Function 'hypoth(x,y)' computes the square root of the sum of the squares of x and y.\n" \
    "       . Function 'normP(u1,...,un)' computes the LP-norm of the specified vector\n" \
    "         (P being an unsigned integer or 'inf').\n" \
    "       . Function 'narg()' returns the number of specified arguments.\n" \
    "       . Function 'arg(i,a_1,..,a_n)' returns the ith argument a_i.\n" \
    "       . Functions 'min()', 'max()', 'med()' and 'kth()' can be called with\n" \
    "         an arbitrary number of arguments.\n" \
    "       . Function 'dowhile(expression)' repeats the evaluation of the expression until it vanishes.\n" \
    "          It can be used to compute mathematical series. For instance the expression:\n" \
    "          'if(N<2,N,n=N-1;F0=0;F1=1;dowhile(F2=F0+F1;F0=F1;F1=F2;n=n-1);F2)' returns the Nth value of the\n" \
    "          Fibonacci sequence, for N>=0 (e.g., 46368 for N=24).\n" \
    "          'dowhile(expression)' always evaluates the specified expression at least once, then check\n" \
    "          for the nullity condition. It always returns 0 when done.\n" \
    "       . Function 'for(init,condition,expression)' first evaluates the expression 'init', then iteratively\n" \
    "          evaluates 'expression' while 'condition' is verified. it may happen that no iteration is done,\n" \
    "          in which case the function returns 0. Otherwise, it returns the last value of 'expression'.\n" \
    "          For instance, the expression: 'if(N<2,N,for(n=N;F0=0;F1=1,n=n-1,F2=F0+F1;F0=F1;F1=F2))' returns\n" \
    "          the Nth value of the Fibonacci sequence, for N>=0 (e.g., 46368 for N=24).\n" \
    "       . Functions 'isval()', 'isnan()', 'isinf()', 'isbool()' can be used to test the type of\n" \
    "          a given number or expression.\n" \
    "       . Function 'isfile()' (resp. 'isdir()') returns 0 (false) or 1 (true) whether its argument\n" \
    "          is a valid path to a file (resp. to a directory) or not.\n" \
    "       . Function 'isin(v,a_1,...,a_n)' returns 0 (false) or 1 (true) whether the first value 'v' appears\n" \
    "          in the set of other values 'a_i'.\n" \
    "       . Function 'fdate(path,attr)' returns the date attribute for the given 'path' (file or directory),\n" \
    "          with 'attr' being { 0=year | 1=month | 2=day | 3=day of week | 4=hour | 5=minute | 6=second }.\n" \
    "       . Function 'date(attr) returns the specified attribute for the current (locale) date\n" \
    "         (same meaning as fdate()).\n" \
    "\n" \
    "    _ Variable names below are pre-defined. They can be overloaded.\n" \
    "         . 'w': width of the associated image, if any (0 otherwise).\n" \
    "         . 'h': height of the associated image, if any (0 otherwise).\n" \
    "         . 'd': depth of the associated image, if any (0 otherwise).\n" \
    "         . 's': spectrum of the associated image, if any (0 otherwise).\n" \
    "         . 'r': shared state of the associated image, if any (0 otherwise).\n" \
    "         . 'wh': shortcut for width*height.\n" \
    "         . 'whd': shortcut for width*height*depth.\n" \
    "         . 'whds': shortcut for width*height*depth*spectrum (i.e. total number of pixel values).\n" \
    "         . 'x': current processed column of the associated image, if any (0 otherwise).\n" \
    "         . 'y': current processed row of the associated image, if any (0 otherwise).\n" \
    "         . 'z': current processed slice of the associated image, if any (0 otherwise).\n" \
    "         . 'c': current processed channel of the associated image, if any (0 otherwise).\n" \
    "         . 't': current time [OpenFX-only].\n" \
    "         . 'k': render scale (1 means full scale, 0.5 means half scale) [OpenFX-only].\n" \
    "         . 'e': value of e, i.e. 2.71828..\n" \
    "         . 'pi': value of pi, i.e. 3.1415926..\n" \
    "         . '?' or 'u': a random value between [0,1], following a uniform distribution.\n" \
    "         . 'g': a random value, following a gaussian distribution of variance 1 (roughly in [-5,5]).\n" \
    "         . 'i': current processed pixel value (i.e. value located at (x,y,z,c)) of the\n" \
    "            associated image, if any (0 otherwise).\n" \
    "         . 'im','iM','ia','iv','ic': Respectively the minimum, maximum, average values,\n" \
    "            variance and median value of the associated image, if any (0 otherwise).\n" \
    "         . 'xm','ym','zm','cm': The pixel coordinates of the minimum value in the associated\n" \
    "            image, if any (0 otherwise).\n" \
    "         . 'xM','yM','zM','cM': The pixel coordinates of the maximum value in the\n" \
    "            associated image, if any (0 otherwise).\n" \
    "\n" \
    "    _ Special operators can be used:\n" \
    "         . ';': expression separator. The returned value is always the last encountered\n" \
    "            expression. For instance expression '1;2;pi' is evaluated as 'pi'.\n" \
    "         . '=': variable assignment. Variables in mathematical parser can only refer to\n" \
    "            numerical values. Variable names are case-sensitive. Use this operator in\n" \
    "            conjunction with ';' to define complex evaluable expressions, such as\n" \
    "            't=cos(x);3*t^2+2*t+1'.\n" \
    "            These variables remain local to the mathematical parser and cannot be accessed\n" \
    "            outside the evaluated expression.\n" \
    "\n" \
    "    _ The following specific functions are also defined:\n" \
    "         . 'if(expr_cond,expr_then,expr_else)': return value of 'expr_then' or 'expr_else',\n" \
    "            depending on the value of 'expr_cond' (0=false, other=true). For instance,\n" \
    "            GMICExpr command 'if(x%10==0,255,i)' will draw blank vertical lines on every\n" \
    "            10th column of an image.\n" \
    "         . '?(max)' or '?(min,max)': return a random value between [0,max] or [min,max],\n" \
    "            following a uniform distribution. 'u(max)' and 'u(0,max)' mean the same.\n" \
    "         . 'i(_a,_b,_c,_d,_interpolation,_boundary)': return the value of the pixel located\n" \
    "            at position (a,b,c,d) in the associated image, if any (0 otherwise).\n" \
    "            Interpolation parameter can be { 0=nearest neighbor | other=linear }.\n" \
    "            Boundary conditions can be { 0=dirichlet | 1=neumann | 2=periodic }.\n" \
    "            Omitted coordinates are replaced by their default values which are respectively\n" \
    "            x, y, z, c and 0.\n" \
    "         . 'j(_dx,_dy,_dz,_dc,_interpolation,_boundary)': does the same for the pixel located\n" \
    "            at position (x+dx,y+dy,z+dz,c+dc).\n" \
    "         . 'i[offset]': return the value of the pixel located at specified offset in the associated\n" \
    "            image buffer.\n" \
    "         . 'j[offset]': does the same for an offset relative to the current pixel (x,y,z,c).\n" \
    "            For instance expression '0.5*(i(x+1)-i(x-1))' will estimate the X-derivative\n" \
    "            of an image with a classical finite difference scheme.\n" \
    "         . If specified formula starts with '>' or '<', the operators 'i(..)' and 'j(..)' will return\n" \
    "            values of the image currently being modified, in forward ('>') or backward ('<') order.\n" \
    "\n" \
    "Sample expressions:\n\n" \
    "'j(sin(y/100/k+t/10)*20*k,sin(x/100/k+t/10)*20*k)' distorts the image with time-varying waves.\n\n" \
    "'0.5*(j(1)-j(-1))' will estimate the X-derivative of an image with a classical finite difference scheme.\n\n" \
    "'if(x%10==0,1,i)' will draw blank vertical lines on every 10th column of an image.\n\n" \
    "'X=x-w/2;Y=y-h/2;D=sqrt(X^2+Y^2);if(D+u*20<80,abs(cos(D/(5+c))),10*(y%(20+c))/255)'\n\n" \
    "'X=x-w/2;Y=y-h/2;D=sqrt(X^2+Y^2);if(D+u*20<80,abs(cos(D/(5+c))),10*(y%(20+c))/255)'\n\n" \
    "'sqrt(zr=-1.2+2.4*x/w;zi=-1.2+2.4*y/h;for(i=0,zr*zr+zi*zi<=4&&i<256,t=zr*zr-zi*zi+0.4;zi=2*zr*zi+0.2;zr=t; i=i+1))/255' draws the Mandelbrot fractal (give it a 1024x1024 image as input).\n" \
    "\n" \
    "Uses the 'fill' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginDescription \
    "Quickly generate or process image from mathematical formula evaluated for each pixel.\n" \
    "Full documentation for G'MIC/CImg expressions can be found at http://gmic.eu/reference.shtml#section9\n" \
    "The only difference is the predefined variables 't' (current time) and 'k' (render scale).\n" \
    "\n" \
    "The mathematical parser understands the following set of functions, operators and variables:\n" \
    "    _ Usual operators, written as in the C or Python programming language.\n" \
    "       See http://gmic.eu/reference.shtml#section9 for the exact syntax.\n" \
    "    _ Usual functions: sin(), cos(), tan(), asin(), acos(), atan(), sinh(), cosh(), tanh(),\n" \
    "       log(), log2(), log10(), exp(), sign(), abs(), atan2(), round(), narg(), arg(),\n" \
    "       isval(), isnan(), isinf(), isint(), isbool(), isdir(), isfile(), rol() (left bit rotation),\n" \
    "       ror() (right bit rotation), min(), max(), med(), kth(), sinc(), int().\n" \
    "       . Function 'atan2()' is the version of 'atan()' with two arguments 'y' and 'x' (as in C/C++).\n" \
    "       . Function 'hypoth(x,y)' computes the square root of the sum of the squares of x and y.\n" \
    "       . Function 'normP(u1,...,un)' computes the LP-norm of the specified vector\n" \
    "         (P being an unsigned integer or 'inf').\n" \
    "       . Function 'narg()' returns the number of specified arguments.\n" \
    "       . Function 'arg(i,a_1,..,a_n)' returns the ith argument a_i.\n" \
    "       . Functions 'min()', 'max()', 'med()' and 'kth()' can be called with\n" \
    "         an arbitrary number of arguments.\n" \
    "       . Function 'dowhile(expression)' repeats the evaluation of the expression until it vanishes.\n" \
    "          It can be used to compute mathematical series.\n" \
    "          'dowhile(expression)' always evaluates the specified expression at least once, then check\n" \
    "          for the nullity condition. It always returns 0 when done.\n" \
    "       . Function 'for(init,condition,expression)' first evaluates the expression 'init', then iteratively\n" \
    "          evaluates 'expression' while 'condition' is verified. it may happen that no iteration is done,\n" \
    "          in which case the function returns 0. Otherwise, it returns the last value of 'expression'.\n" \
    "       . Functions 'isval()', 'isnan()', 'isinf()', 'isbool()' can be used to test the type of\n" \
    "          a given number or expression.\n" \
    "       . Function 'isfile()' (resp. 'isdir()') returns 0 (false) or 1 (true) whether its argument\n" \
    "          is a valid path to a file (resp. to a directory) or not.\n" \
    "       . Function 'isin(v,a_1,...,a_n)' returns 0 (false) or 1 (true) whether the first value 'v' appears\n" \
    "          in the set of other values 'a_i'.\n" \
    "       . Function 'fdate(path,attr)' returns the date attribute for the given 'path' (file or directory),\n" \
    "          with 'attr' being { 0=year | 1=month | 2=day | 3=day of week | 4=hour | 5=minute | 6=second }.\n" \
    "       . Function 'date(attr) returns the specified attribute for the current (locale) date\n" \
    "         (same meaning as fdate()).\n" \
    "\n" \
    "    _ Variable names below are pre-defined. They can be overloaded.\n" \
    "         . 'w': width of the associated image, if any (0 otherwise).\n" \
    "         . 'h': height of the associated image, if any (0 otherwise).\n" \
    "         . 'd': depth of the associated image, if any (0 otherwise).\n" \
    "         . 's': spectrum of the associated image, if any (0 otherwise).\n" \
    "         . 'r': shared state of the associated image, if any (0 otherwise).\n" \
    "         . 'wh': shortcut for width*height.\n" \
    "         . 'whd': shortcut for width*height*depth.\n" \
    "         . 'whds': shortcut for width*height*depth*spectrum (i.e. total number of pixel values).\n" \
    "         . 'x': current processed column of the associated image, if any (0 otherwise).\n" \
    "         . 'y': current processed row of the associated image, if any (0 otherwise).\n" \
    "         . 'z': current processed slice of the associated image, if any (0 otherwise).\n" \
    "         . 'c': current processed channel of the associated image, if any (0 otherwise).\n" \
    "         . 't': current time [OpenFX-only].\n" \
    "         . 'k': render scale (1 means full scale, 0.5 means half scale) [OpenFX-only].\n" \
    "         . 'e': value of e, i.e. 2.71828..\n" \
    "         . 'pi': value of pi, i.e. 3.1415926..\n" \
    "         . '?' or 'u': a random value between [0,1], following a uniform distribution.\n" \
    "         . 'g': a random value, following a gaussian distribution of variance 1 (roughly in [-5,5]).\n" \
    "         . 'i': current processed pixel value (i.e. value located at (x,y,z,c)) of the\n" \
    "            associated image, if any (0 otherwise).\n" \
    "         . 'im','iM','ia','iv','ic': Respectively the minimum, maximum, average values,\n" \
    "            variance and median value of the associated image, if any (0 otherwise).\n" \
    "         . 'xm','ym','zm','cm': The pixel coordinates of the minimum value in the associated\n" \
    "            image, if any (0 otherwise).\n" \
    "         . 'xM','yM','zM','cM': The pixel coordinates of the maximum value in the\n" \
    "            associated image, if any (0 otherwise).\n" \
    "\n" \
    "    _ Special operators can be used:\n" \
    "         . ';': expression separator. The returned value is always the last encountered\n" \
    "            expression. For instance expression '1;2;pi' is evaluated as 'pi'.\n" \
    "         . '=': variable assignment. Variables in mathematical parser can only refer to\n" \
    "            numerical values. Variable names are case-sensitive. Use this operator in\n" \
    "            conjunction with ';' to define complex evaluable expressions, such as\n" \
    "            't=cos(x);3*t^2+2*t+1'.\n" \
    "            These variables remain local to the mathematical parser and cannot be accessed\n" \
    "            outside the evaluated expression.\n" \
    "\n" \
    "    _ The following specific functions are also defined:\n" \
    "         . 'if(expr_cond,expr_then,expr_else)': return value of 'expr_then' or 'expr_else',\n" \
    "            depending on the value of 'expr_cond' (0=false, other=true). For instance,\n" \
    "            GMICExpr command 'if(x%10==0,255,i)' will draw blank vertical lines on every\n" \
    "            10th column of an image.\n" \
    "         . '?(max)' or '?(min,max)': return a random value between [0,max] or [min,max],\n" \
    "            following a uniform distribution. 'u(max)' and 'u(0,max)' mean the same.\n" \
    "         . 'i(_a,_b,_c,_d,_interpolation,_boundary)': return the value of the pixel located\n" \
    "            at position (a,b,c,d) in the associated image, if any (0 otherwise).\n" \
    "            Interpolation parameter can be { 0=nearest neighbor | other=linear }.\n" \
    "            Boundary conditions can be { 0=dirichlet | 1=neumann | 2=periodic }.\n" \
    "            Omitted coordinates are replaced by their default values which are respectively\n" \
    "            x, y, z, c and 0.\n" \
    "         . 'j(_dx,_dy,_dz,_dc,_interpolation,_boundary)': does the same for the pixel located\n" \
    "            at position (x+dx,y+dy,z+dz,c+dc).\n" \
    "         . 'i[offset]': return the value of the pixel located at specified offset in the associated\n" \
    "            image buffer.\n" \
    "         . 'j[offset]': does the same for an offset relative to the current pixel (x,y,z,c).\n" \
    "            For instance expression '0.5*(i(x+1)-i(x-1))' will estimate the X-derivative\n" \
    "            of an image with a classical finite difference scheme.\n" \
    "         . If specified formula starts with '>' or the 'less than' character, the operators\n" \
    "            'i(..)' and 'j(..)' will return values of the image currently being modified, in\n" \
    "            forward ('>') or backward ('less than' character) order.\n" \
    "\n" \
    "Sample expressions:\n\n" \
    "'j(sin(y/100/k+t/10)*20*k,sin(x/100/k+t/10)*20*k)' distorts the image with time-varying waves.\n\n" \
    "'0.5*(j(1)-j(-1))' will estimate the X-derivative of an image with a classical finite difference scheme.\n\n" \
    "'if(x%10==0,1,i)' will draw blank vertical lines on every 10th column of an image.\n\n" \
    "\n" \
    "Uses the 'fill' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgExpression"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

#define kSupportsComponentRemapping 0 // components may be used in the expression, even if not processed
#define kSupportsTiles 0 // Expression effect can only be computed on the whole image
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe
#ifdef cimg_use_openmp
#define kHostFrameThreading false
#else
#define kHostFrameThreading true
#endif
#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsXY true
#define kSupportsAlpha true

#define kParamExpression "expression"
#define kParamExpressionLabel "Expression"
#define kParamExpressionHint "G'MIC/CImg expression, see the plugin description/help, or http://gmic.eu/reference.shtml#section9"
#define kParamExpressionDefault "i"

#define kParamHelp "help"
#define kParamHelpLabel "Help"
#define kParamHelpHint "Display help for writing GMIC expressions."


/// Expression plugin
struct CImgExpressionParams
{
    std::string expr;
};

class CImgExpressionPlugin
    : public CImgFilterPluginHelper<CImgExpressionParams, true>
{
public:

    CImgExpressionPlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgExpressionParams, true>(handle, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true, /*defaultProcessAlphaOnRGBA=*/ false)
    {
        _expr  = fetchStringParam(kParamExpression);
        assert(_expr);
    }

    virtual void getValuesAtTime(double time,
                                 CImgExpressionParams& params) OVERRIDE FINAL
    {
        _expr->getValueAtTime(time, params.expr);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& /*renderScale*/,
                        const CImgExpressionParams& /*params*/,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        roi->x1 = rect.x1;
        roi->x2 = rect.x2;
        roi->y1 = rect.y1;
        roi->y2 = rect.y2;
    }

    virtual void render(const OFX::RenderArguments &args,
                        const CImgExpressionParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if ( params.expr.empty() ) {
            throwSuiteStatusException(kOfxStatFailed);
        }
        char vars[256];
        snprintf(vars, sizeof(vars), "t=%g;k=%g;", args.time, args.renderScale.x);
        std::string expr;
        if ( (params.expr[0] == '<') || (params.expr[0] == '>') ) {
            expr = params.expr.substr(0, 1) + vars + params.expr.substr(1);
        } else {
            expr = vars + params.expr;
        }
        try {
            cimg.fill(expr.c_str(), true);
        } catch (const cimg_library::CImgArgumentException& e) {
            setPersistentMessage( OFX::Message::eMessageError, "", e.what() );
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments & /*args*/,
                            const CImgExpressionParams& /*params*/) OVERRIDE FINAL
    {
        // must clear persistent message in isIdentity, or render() is not called by Nuke
        clearPersistentMessage();

        return false;
    };

    /* Override the clip preferences, we need to say we are setting the frame varying flag */
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL
    {
        clipPreferences.setOutputFrameVarying(true);
        clipPreferences.setOutputHasContinousSamples(true);
    }

    virtual void changedParam(const OFX::InstanceChangedArgs &args,
                              const std::string &paramName) OVERRIDE FINAL
    {
        if (paramName == kParamHelp) {
            sendMessage(OFX::Message::eMessageMessage, "", kPluginDescriptionUnsafe);
        } else {
            CImgFilterPluginHelper<CImgExpressionParams, true>::changedParam(args, paramName);
        }
    }

private:

    // params
    OFX::StringParam *_expr;
};


mDeclarePluginFactory(CImgExpressionPluginFactory, {}, {});

void
CImgExpressionPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
#ifdef DEBUG
    desc.setPluginDescription(kPluginDescription);
#else
    if ( OFX::getImageEffectHostDescription()->isNatron &&
         ( OFX::getImageEffectHostDescription()->versionMajor >= 2) ) {
        desc.setPluginDescription(kPluginDescriptionUnsafe);
    } else {
        desc.setPluginDescription(kPluginDescription);
    }
#endif
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

void
CImgExpressionPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc,
                                               OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgExpressionPlugin::describeInContextBegin(desc, context,
                                                                                  kSupportsRGBA,
                                                                                  kSupportsRGB,
                                                                                  kSupportsXY,
                                                                                  kSupportsAlpha,
                                                                                  kSupportsTiles,
                                                                                  /*processRGB=*/ true,
                                                                                  /*processAlpha*/ false,
                                                                                  /*processIsSecret=*/ false);
    {
        OFX::StringParamDescriptor *param = desc.defineStringParam(kParamExpression);
        param->setLabel(kParamExpressionLabel);
        param->setHint(kParamExpressionHint);
        param->setDefault(kParamExpressionDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        OFX::PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamHelp);
        param->setLabel(kParamHelpLabel);
        param->setHint(kParamHelpHint);
        if (page) {
            page->addChild(*param);
        }
    }
    CImgExpressionPlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect*
CImgExpressionPluginFactory::createInstance(OfxImageEffectHandle handle,
                                            OFX::ContextEnum /*context*/)
{
    return new CImgExpressionPlugin(handle);
}

static CImgExpressionPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
