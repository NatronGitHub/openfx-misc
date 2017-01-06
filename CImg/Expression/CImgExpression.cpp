/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2017 INRIA
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
    "The only difference is the predefined variables 'T' (current time) and 'K' (render scale).\n" \
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
    "         . 'T': current time [OpenFX-only].\n" \
    "         . 'K': render scale (1 means full scale, 0.5 means half scale) [OpenFX-only].\n" \
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
    "Sample expressions:\n" \
    "\n" \
    "'j(sin(y/100/k+t/10)*20*k,sin(x/100/k+t/10)*20*k)' distorts the image with time-varying waves.\n" \
    "\n" \
    "'0.5*(j(1)-j(-1))' will estimate the X-derivative of an image with a classical finite difference scheme.\n" \
    "\n" \
    "'if(x%10==0,1,i)' will draw blank vertical lines on every 10th column of an image.\n" \
    "\n" \
    "'X=x-w/2;Y=y-h/2;D=sqrt(X^2+Y^2);if(D+u*20<80,abs(cos(D/(5+c))),10*(y%(20+c))/255)'\n" \
    "\n" \
    "'X=x-w/2;Y=y-h/2;D=sqrt(X^2+Y^2);if(D+u*20<80,abs(cos(D/(5+c))),10*(y%(20+c))/255)'\n" \
    "\n" \
    "'sqrt(zr=-1.2+2.4*x/w;zi=-1.2+2.4*y/h;for(i=0,zr*zr+zi*zi<=4&&i<256,t=zr*zr-zi*zi+0.4;zi=2*zr*zi+0.2;zr=t; i=i+1))/255' draws the Mandelbrot fractal (give it a 1024x1024 image as input).\n" \
    "\n" \
    "Uses the 'fill' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginDescription \
    "Quickly generate or process image from mathematical formula evaluated for each pixel.\n" \
    "Full documentation for G'MIC/CImg expressions can be found at http://gmic.eu/reference.shtml#section9\n" \
    "The only difference is the predefined variables 'T' (current time) and 'K' (render scale).\n" \
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
    "         . 'T': current time [OpenFX-only].\n" \
    "         . 'K': render scale (1 means full scale, 0.5 means half scale) [OpenFX-only].\n" \
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
    "Sample expressions:\n" \
    "\n" \
    "'j(sin(y/100/k+t/10)*20*k,sin(x/100/k+t/10)*20*k)' distorts the image with time-varying waves.\n" \
    "\n" \
    "'0.5*(j(1)-j(-1))' will estimate the X-derivative of an image with a classical finite difference scheme.\n" \
    "\n" \
    "'if(x%10==0,1,i)' will draw blank vertical lines on every 10th column of an image.\n" \
    "\n" \
    "\n" \
    "Uses the 'fill' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginDescriptionMarkdown \
    "Quickly generate or process image from mathematical formula evaluated for each pixel.\n" \
    "Full documentation for [G'MIC](http://gmic.eu/)/[CImg](http://cimg.eu/) expressions is reproduced below and available online from the [G'MIC help](http://gmic.eu/reference.shtml#section9).\n" \
    "The only additions of this plugin are the predefined variables `T` (current time) and `K` (render scale).\n" \
    "\n" \
    "Uses the 'fill' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C (close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. It can be used in commercial applications (see http://cimg.eu).\n" \
    "\n" \
    "### Sample expressions:\n" \
    "\n" \
    "- `j(sin(y/100/K+T/10)*20*K,sin(x/100/K+T/10)*20*K)'` distorts the image with time-varying waves.\n" \
    "- `0.5*(j(1)-j(-1))` will estimate the X-derivative of an image with a classical finite difference scheme.\n" \
    "- `if(x%10==0,1,i)` will draw blank vertical lines on every 10th column of an image.\n" \
    "\n" \
    "### Expression language\n" \
    "  - The expression is evaluated for each pixel of the selected images.\n" \
    "\n" \
    "  - The mathematical parser understands the following set of functions, operators and variables:\n" \
    "\n" \
    "    + Usual operators: `||` (logical or), `&&` (logical and), `|` (bitwise or), `&` (bitwise and), `!=`, `==`, `<=`, `>=`, `<`, `>`, `<<` (left bitwise shift), `>>` (right bitwise shift), `-`, `+`, `*`, `/`, `%` (modulo), `^` (power), `!` (logical not), `~` (bitwise not), `++`, `--`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `>>=`, `<<=` (in-place operators).\n" \
    "\n" \
    "    + Usual functions: `abs()`, `acos()`, `arg()`, `argmax()`, `argmin()`, `asin()`, `atan()`, `atan2()`, `cbrt()`, `cos()`, `cosh()`, `cut()`, `exp()`, `fact()`, `fibo()`, `gauss()`, `hypoth()`, `int()`, `isval()`, `isnan()`, `isinf()`, `isint()`, `isbool()`, `isfile()`, `isdir()`, `isin()`, `kth()`, `log()`, `log2()`, `log10()`, `max()`, `mean()`, `med()`, `min()`, `narg()`, `prod()`, `rol()` (left bit rotation), `ror()` (right bit rotation), `round()`, `sign()`, `sin()`, `sinc()`, `sinh()`, `sqrt()`, `std()`, `sum()`, `tan()`, `tanh()`, `variance()`.\n" \
    "\n" \
    "       * `atan2(x,y)` is the version of `atan()` with two arguments `y` and `x` (as in C/C++).\n" \
    "       * `hypoth(x,y)` computes the square root of the sum of the squares of x and y.\n" \
    "       * `permut(k,n,with_order)` computes the number of permutations of k objects from a set of k objects.\n" \
    "       * `gauss(x,_sigma)` returns `exp(-x^2/(2*s^2))/sqrt(2*pi*sigma^2)`.\n" \
    "       * `cut(value,min,max)` returns value if it is in range [min,max], or min or max otherwise.\n" \
    "       * `narg(a_1,...,a_N)` returns the number of specified arguments (here, N).\n" \
    "       * `arg(i,a_1,..,a_N)` returns the ith argument a_i.\n" \
    "       * `isval()`, `isnan()`, `isinf()`, `isint()`, `isbool()` test the type of the given number or expression, and return 0 (false) or 1 (true).\n" \
    "       * `isfile()` (resp. `isdir()`) returns 0 (false) or 1 (true) whether its argument is a valid path to a file (resp. to a directory) or not.\n" \
    "       * `isin(v,a_1,...,a_n)` returns 0 (false) or 1 (true) whether the first value `v` appears in the set of other values `a_i`.\n" \
    "       * `argmin()`, `argmax()`, `kth()`, `max()`, `mean()`, `med()`, `min()`, `std()`, `sum()` and `variance()` can be called with an arbitrary number of scalar/vector arguments.\n" \
    "       * `round(value,rounding_value,direction)` returns a rounded value. `direction` can be { -1=to-lowest | 0=to-nearest | 1=to-highest }.\n" \
    "\n" \
    "    + Variable names below are pre-defined. They can be overrided.\n" \
    "\n" \
    "       * `l`: length of the associated list of images, if any (0 otherwise).\n" \
    "       * `w`: width of the associated image, if any (0 otherwise).\n" \
    "       * `h`: height of the associated image, if any (0 otherwise).\n" \
    "       * `d`: depth of the associated image, if any (0 otherwise).\n" \
    "       * `s`: spectrum of the associated image, if any (0 otherwise).\n" \
    "       * `r`: shared state of the associated image, if any (0 otherwise).\n" \
    "       * `wh`: shortcut for width x height.\n" \
    "       * `whd`: shortcut for width x height x depth.\n" \
    "       * `whds`: shortcut for width x height x depth x spectrum (i.e. total number of pixel values).\n" \
    "       * `i`: current processed pixel value (i.e. value located at (x,y,z,c)) in the associated image, if any (0 otherwise).\n" \
    "       * `iN`: Nth channel value of current processed pixel (i.e. value located at (x,y,z,N)) in the associated image, if any (0 otherwise). `N` must be an integer in range [0,7].\n" \
    "       * `R`,`G`,`B` and `A` are equivalent to `i0`, `i1`, `i2` and `i3` respectively.\n" \
    "       * `im`,`iM`,`ia`,`iv`,`is`,`ip`,`ic`: Respectively the minimum, maximum, average values, variance, sum, product and median value of the associated image, if any (0 otherwise).\n" \
    "       * `xm`,`ym`,`zm`,`cm`: The pixel coordinates of the minimum value in the associated image, if any (0 otherwise).\n" \
    "       * `xM`,`yM`,`zM`,`cM`: The pixel coordinates of the maximum value in the associated image, if any (0 otherwise).\n" \
    "       * You may add `#ind` to any of the variable name above to retrieve the information for any numbered image `[ind]` of the list (when this makes sense). For instance `ia#0` denotes the average value of the first image).\n" \
    "       * `x`: current processed column of the associated image, if any (0 otherwise).\n" \
    "       * `y`: current processed row of the associated image, if any (0 otherwise).\n" \
    "       * `z`: current processed slice of the associated image, if any (0 otherwise).\n" \
    "       * `c`: current processed channel of the associated image, if any (0 otherwise).\n" \
    "       * `t`: thread id when an expression is evaluated with multiple threads (0 means `master thread`).\n" \
    "       * `T`: current time [OpenFX-only].\n" \
    "       * `K`: render scale (1 means full scale, 0.5 means half scale) [OpenFX-only].\n" \
    "       * `e`: value of e, i.e. 2.71828..\n" \
    "       * `pi`: value of pi, i.e. 3.1415926..\n" \
    "       * `u`: a random value between [0,1], following a uniform distribution.\n" \
    "       * `g`: a random value, following a gaussian distribution of variance 1 (roughly in [-6,6]).\n" \
    "       * `interpolation`: value of the default interpolation mode used when reading pixel values with the pixel access operators (i.e. when the interpolation argument is not explicitly specified, see below for more details on pixel access operators). Its initial default value is 0.\n" \
    "       * `boundary`: value of the default boundary conditions used when reading pixel values with the pixel access operators (i.e. when the boundary condition argument is not explicitly specified, see below for more details on pixel access operators). Its initial default value is 0.\n" \
    "\n" \
    "    + Vector calculus: Most operators are also able to work with vector-valued elements.\n" \
    "\n" \
    "       * `[ a0,a1,..,aN ]` defines a (N+1)-dimensional vector with specified scalar coefficients ak.\n" \
    "       * `vectorN(a0,a1,,..,)` does the same, with the ak being repeated periodically.\n" \
    "       * In both expressions, the ak can be vectors themselves, to be concatenated into a single vector.\n" \
    "       * The scalar element ak of a vector X is retrieved by X[k].\n" \
    "       * The sub-vector [ ap..aq ] of a vector X is retrieved by X[p,q].\n" \
    "       * Equality/inequality comparisons between two vectors is possible with the operators `==` and `!=`.\n" \
    "       * Some vector-specific functions can be used on vector values: `cross(X,Y)` (cross product), `dot(X,Y)` (dot product), `size(X)` (vector dimension), `sort(X,_is_increasing,_chunk_size)` (sorting values), `reverse(A)` (reverse order of components) and `same(A,B,_nb_vals,_is_case_sensitive)` (vector equality test).\n" \
    "       * Function `resize(A,size,_interpolation)` returns a resized version of vector `A` with specified interpolation mode. `interpolation`  can be { -1=none (memory content) | 0=none | 1=nearest | 2=average | 3=linear | 4=grid | 5=bicubic | 6=lanczos }.\n" \
    "       * Function `find(A,B,_is_forward,_starting_indice)` returns the index where sub-vector B appears in vector A, (or -1 if B is not found in A). Argument A can be also replaced by an image indice #ind.\n" \
    "       * A 2-dimensional vector may be seen as a complex number and used in those particular functions/operators: `**` (complex multiplication), `//` (complex division), `^^` (complex exponentiation), `**=` (complex self-multiplication), `//=` (complex self-division), `^^=` (complex self-exponentiation), `cabs()` (complex modulus), `carg()` (complex argument), `cconj()` (complex conjugate), `cexp()` (complex exponential) and `clog()` (complex logarithm).\n" \
    "       * A MN-dimensional vector may be seen as a M x N matrix and used in those particular functions/operators: `**` (matrix-vector multiplication), `det(A)` (determinant), `diag(V)` (diagonal matrix from vector), `eig(A)` (eigenvalues/eigenvectors), `eye(n)` (n x n identity matrix), `inv(A)` (matrix inverse), `mul(A,B,_nb_colsB)` (matrix-matrix multiplication), `rot(x,y,z,angle)` (3d rotation matrix), `rot(angle)` (2d rotation matrix), `solve(A,B,_nb_colsB)` (least-square solver of linear system A.X = B), `trace(A)` (matrix trace) and `transp(A,nb_colsA)` (matrix transpose). Argument `nb_colsB` may be omitted if equal to 1.\n" \
    "       * Specifying a vector-valued math expression as an expression modifies the whole spectrum range of the processed image(s), for each spatial coordinates (x,y,z). The command does not loop over the C-axis in this case.\n" \
    "\n" \
    "    + String manipulation: Character strings are defined and managed as vectors objects.\n" \
    "       Dedicated functions and initializers to manage strings are\n" \
    "\n" \
    "       * `[ 'string' ]` and `'string'` define a vector whose values are the ascii codes of the specified character string (e.g. `'foo'` is equal to `[ 102,111,111 ]`).\n" \
    "       * `_'character'` returns the (scalar) ascii code of the specified character (e.g. `_'A'` is equal to 65).\n" \
    "       * A special case happens for empty strings: Values of both expressions `[ '' ]` and `''` are 0.\n" \
    "       * Functions `lowercase()` and `uppercase()` return string with all string characters lowercased or uppercased.\n" \
    "\n" \
    "    + Special operators can be used:\n" \
    "\n" \
    "       * `;`: expression separator. The returned value is always the last encountered expression. For instance expression `1;2;pi` is evaluated as `pi`.\n" \
    "       * `=`: variable assignment. Variables in mathematical parser can only refer to numerical values. Variable names are case-sensitive. Use this operator in conjunction with `;` to define more complex evaluable expressions, such as `t=cos(x);3*t^2+2*t+1`. These variables remain local to the mathematical parser and cannot be accessed outside the evaluated expression.\n" \
    "\n" \
    "    + The following specific functions are also defined:\n" \
    "\n" \
    "       * `normP(u1,...,un)` computes the LP-norm of the specified vector (P being an unsigned integer or `inf`).\n" \
    "       * `u(max)` or `u(min,max)`: return a random value between [0,max] or [min,max], following a uniform distribution.\n" \
    "       * `i(_a,_b,_c,_d,_interpolation_type,_boundary_conditions)`: return the value of the pixel located at position (a,b,c,d) in the associated image, if any (0 otherwise). `interpolation_type` can be { 0=nearest neighbor | other=linear }. `boundary_conditions` can be { 0=dirichlet | 1=neumann | 2=periodic }. Omitted coordinates are replaced by their default values which are respectively x, y, z, c, interpolation and boundary. For instance expression `0.5*(i(x+1)-i(x-1))` will estimate the X-derivative of an image with a classical finite difference scheme.\n" \
    "       * `j(_dx,_dy,_dz,_dc,_interpolation_type,_boundary_conditions)` does the same for the pixel located at position (x+dx,y+dy,z+dz,c+dc) (pixel access relative to the current coordinates).\n" \
    "       * `i[offset,_boundary_conditions]` returns the value of the pixel located at specified `offset` in the associated image buffer (or 0 if offset is out-of-bounds).\n" \
    "       * `j[offset,_boundary_conditions]` does the same for an offset relative to the current pixel (x,y,z,c).\n" \
    "       * `i(#ind,_x,_y,_z,_c,_interpolation,_boundary)`, `j(#ind,_dx,_dy,_dz,_dc,_interpolation,_boundary)`, `i[#ind,offset,_boundary]` and `i[offset,_boundary]` are similar expressions used to access pixel values for any numbered image `[ind]` of the list.\n" \
    "       * `I/J[offset,_boundary_conditions]` and `I/J(#ind,_x,_y,_z,_interpolation,_boundary)` do the same as `i/j[offset,_boundary_conditions]` and `i/j(#ind,_x,_y,_z,_c,_interpolation,_boundary)` but return a vector instead of a scalar (e.g. a vector [ R,G,B ] for a pixel at (a,b,c) in a color image).\n" \
    "       * `crop(_#ind,_x,_y,_z,_c,_dx,_dy,_dz,_dc,_boundary)` returns a vector whose values come from the cropped region of image `[ind]` (or from default image selected if `ind` is not specified). Cropped region starts from point (x,y,z,c) and has a size of dx x dy x dz x dc. Arguments for coordinates and sizes can be omitted if they are not ambiguous (e.g. `crop(#ind,x,y,dx,dy)` is a valid invokation of this function).\n" \
    "       * `draw(_#ind,S,x,y,z,c,dx,_dy,_dz,_dc,_opacity,_M,_max_M)` draws a sprite S in image `[ind]` (or in default image selected if `ind` is not specified) at specified coordinates (x,y,z,c). The size of the sprite dx x dy x dz x dc must be specified. You can also specify a corresponding opacity mask M if its size matches S.\n" \
    "       * `if(condition,expr_then,_expr_else)`: return value of `expr_then` or `expr_else`, depending on the value of `condition` (0=false, other=true). `expr_else` can be omitted in which case 0 is returned if the condition does not hold. Using the ternary operator `condition?expr_then[:expr_else]` gives an equivalent expression. For instance, G'MIC expressions `if(x%10==0,255,i)` and `x%10?i:255` both draw blank vertical lines on every 10th column of an image.\n" \
    "       * `dowhile(expression,_condition)` repeats the evaluation of `expression` until `condition` vanishes (or until `expression` vanishes if no `condition` is specified). For instance, the expression: `if(N<2,N,n=N-1;F0=0;F1=1;dowhile(F2=F0+F1;F0=F1;F1=F2,n=n-1))` returns the Nth value of the Fibonacci sequence, for N>=0 (e.g., 46368 for N=24). `dowhile(expression,condition)` always evaluates the specified expression at least once, then check for the nullity condition. When done, it returns the last value of `expression`.\n" \
    "       * `for(init,condition,_procedure,body)` first evaluates the expression `init`, then iteratively evaluates `body` (followed by `procedure` if specified) while `condition` is verified (i.e. not zero). It may happen that no iteration is done, in which case the function returns 0. Otherwise, it returns the last value of `body`. For instance, the expression: `if(N<2,N,for(n=N;F0=0;F1=1,n=n-1,F2=F0+F1;F0=F1;F1=F2))` returns the Nth value of the Fibonacci sequence, for N>=0 (e.g., 46368 for N=24).\n" \
    "       * `whiledo(condition,expression)` is exactly the same as `for(init,condition,expression)` without the specification of an initializing expression.\n" \
    "       * `date(attr,path)` returns the date attribute for the given `path` (file or directory), with `attr` being { 0=year | 1=month | 2=day | 3=day of week | 4=hour | 5=minute | 6=second }.\n" \
    "       * `date(_attr)` returns the specified attribute for the current (locale) date.\n" \
    "       * `print(expression)` prints the value of the specified expression on the console (and returns its value).\n" \
    "       * `debug(expression)` prints detailed debug information about the sequence of operations done by the math parser to evaluate the expression (and returns its value).\n" \
    "       * `init(expression)` evaluates the specified expression only once, even when multiple evaluations are required (e.g. in `init(foo=0);++foo`).\n" \
    "       * `copy(dest,src,_nb_elts,_inc_d,_inc_s)` copies an entire memory block of `nb_elts` elements starting from a source value `src` to a specified destination `dest`, with increments defined by `inc_d` and `inc_s` respectively for the destination and source pointers.\n" \
    "\n" \
    "    + User-defined functions:\n" \
    "\n" \
    "       * Custom macro functions can be defined in a math expression, using the assignment operator `=`, e.g. `foo(x,y) = cos(x + y); result = foo(1,2) + foo(2,3)`.\n" \
    "       * Overriding a built-in function has no effect.\n" \
    "       * Overriding an already defined macro function replaces its old definition.\n" \
    "       * Macro functions are indeed processed as macros by the mathematical evaluator. You should avoid invoking them with arguments that are themselves results of assignments or self-operations. For instance, `foo(x) = x + x; z = 0; result = foo(++x)` will set `result = 4` rather than expected value `2`.\n" \
    "\n" \
    "    + Multi-threaded and in-place evaluation:\n" \
    "\n" \
    "       * If your image data are large enough and you have several CPUs available, it is likely that the math expression is evaluated in parallel, using multiple computation threads.\n" \
    "       * Starting an expression with `:` or `*` forces the evaluations required for an image to be run in parallel, even if the amount of data to process is small (beware, it may be slower to evaluate!). Specify `:` (instead of `*`) to avoid possible image copy done before evaluating the expression (this saves memory, but do this only if you are sure this step is not required!)\n" \
    "       * If the specified expression starts with `>` or `<`, the pixel access operators `i()`, `i[]`, `j()` and `j[]` return values of the image being currently modified, in forward (`>`) or backward (`<`) order. The multi-threading evaluation of the expression is also disabled in this case.\n" \
    "       * Function `(operands)` forces the execution of the given operands in a single thread at a time.\n" \
    "\n" \
    "    + Expressions `i(_#ind,x,_y,_z,_c)=value`, `j(_#ind,x,_y,_z,_c)=value`, `i[_#ind,offset]=value` and `j[_#ind,offset]=value` set a pixel value at a different location than the running one in the image `[ind]` (or in the associated image if argument `#ind` is omitted), either with global coordinates/offsets (with `i(...)` and `i[...]`), or relatively to the current position (x,y,z,c) (with `j(...)` and `j[...]`). These expressions always return `value`.\n" \
    ""

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
        : CImgFilterPluginHelper<CImgExpressionParams, true>(handle, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
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

    virtual void render(const RenderArguments &args,
                        const CImgExpressionParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if ( params.expr.empty() ) {
            throwSuiteStatusException(kOfxStatFailed);
        }
        char vars[256];
        snprintf(vars, sizeof(vars), "T=%g;K=%g;", args.time, args.renderScale.x);
        std::string expr;
        if ( (params.expr[0] == '<') || (params.expr[0] == '>') ) {
            expr = params.expr.substr(0, 1) + vars + params.expr.substr(1);
        } else {
            expr = vars + params.expr;
        }
        try {
            cimg.fill(expr.c_str(), true);
        } catch (const cimg_library::CImgArgumentException& e) {
            setPersistentMessage( Message::eMessageError, "", e.what() );
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgExpressionParams& /*params*/) OVERRIDE FINAL
    {
        // must clear persistent message in isIdentity, or render() is not called by Nuke
        clearPersistentMessage();

        return false;
    };

    /* Override the clip preferences, we need to say we are setting the frame varying flag */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL
    {
        clipPreferences.setOutputFrameVarying(true);
        clipPreferences.setOutputHasContinuousSamples(true);
    }

    virtual void changedParam(const InstanceChangedArgs &args,
                              const std::string &paramName) OVERRIDE FINAL
    {
        if (paramName == kParamHelp) {
            sendMessage(Message::eMessageMessage, "", kPluginDescriptionUnsafe);
        } else {
            CImgFilterPluginHelper<CImgExpressionParams, true>::changedParam(args, paramName);
        }
    }

private:

    // params
    StringParam *_expr;
};


mDeclarePluginFactory(CImgExpressionPluginFactory, {}, {});

void
CImgExpressionPluginFactory::describe(ImageEffectDescriptor& desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    if ( desc.getPropertySet().propGetDimension(kNatronOfxPropDescriptionIsMarkdown, false) ) {
        desc.setPluginDescription(kPluginDescriptionMarkdown, false);
        desc.setDescriptionIsMarkdown(true);
    } else if ( getImageEffectHostDescription()->isNatron &&
                ( getImageEffectHostDescription()->versionMajor >= 2) ) {
        desc.setPluginDescription(kPluginDescriptionUnsafe);
    } else {
        desc.setPluginDescription(kPluginDescription);
    }

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
CImgExpressionPluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                               ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgExpressionPlugin::describeInContextBegin(desc, context,
                                                                             kSupportsRGBA,
                                                                             kSupportsRGB,
                                                                             kSupportsXY,
                                                                             kSupportsAlpha,
                                                                             kSupportsTiles,
                                                                             /*processRGB=*/ true,
                                                                             /*processAlpha*/ false,
                                                                             /*processIsSecret=*/ false);
    {
        StringParamDescriptor *param = desc.defineStringParam(kParamExpression);
        param->setLabel(kParamExpressionLabel);
        param->setHint(kParamExpressionHint);
        param->setDefault(kParamExpressionDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamHelp);
        param->setLabel(kParamHelpLabel);
        param->setHint(kParamHelpHint);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgExpressionPlugin::describeInContextEnd(desc, context, page);
}

ImageEffect*
CImgExpressionPluginFactory::createInstance(OfxImageEffectHandle handle,
                                            ContextEnum /*context*/)
{
    return new CImgExpressionPlugin(handle);
}

static CImgExpressionPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
