Quickly generate or process image from mathematical formula evaluated for each pixel.
Full documentation for [G'MIC](http://gmic.eu/)/[CImg](http://cimg.eu/) expressions is reproduced below and available online from the [G'MIC help](http://gmic.eu/reference.shtml#section9).
The only additions of this plugin are the predefined variables `T` (current time) and `K` (render scale).

Uses the 'fill' function from the CImg library.
CImg is a free, open-source library distributed under the CeCILL-C (close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. It can be used in commercial applications (see http://cimg.eu).

### Sample expressions

- ' `j(sin(y/100/K+T/10)*20*K,sin(x/100/K+T/10)*20*K)`' distorts the image with time-varying waves.
- '`0.5*(j(1)-j(-1))`' estimates the X-derivative of an image with a classical finite difference scheme.
- '`if(x%10==0,1,i)`' draws blank vertical lines on every 10th column of an image.
- '`sqrt(zr=-1.2+2.4*x/w;zi=-1.2+2.4*y/h;for(i=0,zr*zr+zi*zi<=4&&i<256,t=zr*zr-zi*zi+0.4;zi=2*zr*zi+0.2;zr=t; i=i+1))/255`' draws the Mandelbrot fractal (give it a 1024x1024 image as input).

### Expression language

- The expression is evaluated for each pixel of the selected images.
- The mathematical parser understands the following set of functions, operators and variables:
    + __Usual operators:__ `||` (logical or), `&&` (logical and), `|` (bitwise or), `&` (bitwise and), `!=`, `==`, `<=`, `>=`, `<`, `>`, `<<` (left bitwise shift), `>>` (right bitwise shift), `-`, `+`, `*`, `/`, `%` (modulo), `^` (power), `!` (logical not), `~` (bitwise not),
       `++`, `--`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `>>=`, `<<=` (in-place operators).
    + __Usual math functions:__ `abs()`, `acos()`, `arg()`, `argkth()`, `argmax()`, `argmin()`, `asin()`, `atan()`, `atan2()`, `avg()`, `bool()`, `cbrt()`, `ceil()`, `cos()`, `cosh()`, `cut()`, `exp()`, `fact()`, `fibo()`, `floor()`, `gauss()`, `int()`, `isval()`, `isnan()`, `isinf()`, `isint()`, `isbool()`, `isfile()`, `isdir()`, `isin()`, `kth()`, `log()`, `log2()`, `log10()`, `max()`, `mean()`, med()`, `min()`, `narg()`, `prod()`, `rol()` (left bit rotation), `ror()` (right bit rotation), `round()`, `sign()`, `sin()`, `sinc()`, `sinh()`, `sqrt()`, `std()`, `srand(_seed)`, `sum()`, `tan()`, `tanh()`, `variance()`, `xor()`.
        * '`atan2(y,x)`' is the version of '`atan()`' with two arguments __'y'__ and __'x'__ (as in C/C\+\+).
        * '`permut(k,n,with_order)`' computes the number of permutations of __k__ objects from a set of __n__ objects.
        * '`gauss(x,_sigma)`' returns __'exp(-x\^2/(2\*s\^2))/sqrt(2\*pi\*sigma\^2)'__.
        * '`cut(value,min,max)`' returns value if it is in range __\[min,max\]__, or __min__ or __max__ otherwise.
        * '`narg(a_1,...,a_N)`' returns the number of specified arguments (here, __N__).
        * '`arg(i,a_1,..,a_N)`' returns the __ith__ argument __a_i__.
        * '`isval()`', '`isnan()`', '`isinf()`', '`isint()`', '`isbool()`' test the type of the given number or expression, and return __0 (false)__ or __1 (true)__.
        * '`isfile()`' (resp. '`isdir()`') returns __0 (false)__ or __1 (true)__ whether its argument is a path to an existing file (resp. to a directory) or not.
        * '`isin(v,a_1,...,a_n)`' returns __0 (false)__ or __1 (true)__ whether the first value __'v'__ appears in the set of other values 'a_i'.
        * '`argmin()`', '`argmax()`', '`kth()`', '`max()`', '`mean()`', '`med()`', '`min()`', '`std()`', '`sum()`' and '`variance()`' can be called with an arbitrary number of scalar/vector arguments.
        * '`round(value,rounding_value,direction)`' returns a rounded value. __'direction'__ can be __\{ -1=to-lowest \| 0=to-nearest \| 1=to-highest \}__.
    + __Variable names__ below are pre-defined. They can be overridden.
        * '`l`': length of the associated list of images.
        * '`w`': width of the associated image, if any (__0__ otherwise).
        * '`h`': height of the associated image, if any (__0__ otherwise).
        * '`d`': depth of the associated image, if any (__0__ otherwise).
        * '`s`': spectrum of the associated image, if any (__0__ otherwise).
        * '`r`': shared state of the associated image, if any (__0__ otherwise).
        * '`wh`': shortcut for width x height.
        * '`whd`': shortcut for width x height x depth.
        * '`whds`': shortcut for width x height x depth x spectrum (i.e. number of image values).
        * '`im`','`iM`','`ia`','`iv`','`is`','`ip`','`ic`': Respectively the minimum, maximum, average, variance, sum, product and median value of the associated image, if any (__0__ otherwise).
        * '`xm`','`ym`','`zm`','`cm`': The pixel coordinates of the minimum value in the associated image, if any (__0__ otherwise).
        * '`xM`','`yM`','`zM`','`cM`': The pixel coordinates of the maximum value in the associated image, if any (__0__ otherwise).
        * All these variables are considered as __constant values__ by the math parser (for optimization purposes) which is indeed the case most of the time. Anyway, this might not be the case, if function '`resize(#ind,..)`' is used in the math expression. If so, it is safer to invoke functions '`l()`', '`w(_#ind)`', '`h(_#ind)`', ... '`s(_#ind)`' and '`ic(_#ind)`' instead of the corresponding named variables.
        * '`i`': current processed pixel value (i.e. value located at __(x,y,z,c)__) in the associated image, if any (__0__ otherwise).
        * '`iN`': Nth channel value of current processed pixel (i.e. value located at __(x,y,z,N)__) in the associated image, if any (__0__ otherwise). __'N'__ must be an integer in range __\[0,9\]__.
        * '`R`','`G`','`B`' and '`A`' are equivalent to '`i0`', '`i1`', '`i2`' and '`i3`' respectively.
        * '`I`': current vector-valued processed pixel in the associated image, if any (__0__ otherwise). The number of vector components is equal to the number of image channels (e.g. __I = \[ R,G,B \]__ for a __RGB__ image).
        * You may add '`#ind`' to any of the variable name above to retrieve the information for any numbered image __\[ind\]__ of the list (when this makes sense). For instance '`ia#0`' denotes the average value of the first image of the list).
        * '`x`': current processed column of the associated image, if any (__0__ otherwise).
        * '`y`': current processed row of the associated image, if any (__0__ otherwise).
        * '`z`': current processed slice of the associated image, if any (__0__ otherwise).
        * '`c`': current processed channel of the associated image, if any (__0__ otherwise).
        * '`t`': thread id when an expression is evaluated with multiple threads (__0__ means 'master thread').
        * '`T`': current time \[OpenFX-only\].
        * '`K`': render scale (1 means full scale, 0.5 means half scale) \[OpenFX-only\].
        * '`e`': value of e, i.e. __2.71828...__
        * '`pi`': value of pi, i.e. __3.1415926...__
        * '`u`': a random value between __\[0,1\]__, following a uniform distribution.
        * '`g`': a random value, following a gaussian distribution of variance 1 (roughly in __\[-6,6\]__).
        * '`interpolation`': value of the default interpolation mode used when reading pixel values  with the pixel access operators (i.e. when the interpolation argument is not explicitly  specified, see below for more details on pixel access operators). Its initial default  value is __0__.
        * '`boundary`': value of the default boundary conditions used when reading pixel values with  the pixel access operators (i.e. when the boundary condition argument is not explicitly  specified, see below for more details on pixel access operators). Its initial default  value is __0__.
    + __Vector calculus:__ Most operators are also able to work with vector-valued elements.
        * '`[ a0,a1,...,aN ]`' defines a __(N+1)__-dimensional vector with scalar coefficients __ak__.
        * '`vectorN(a0,a1,,...,)`' does the same, with the __ak__ being repeated periodically if only a few are specified.
        * In both previous expressions, the __ak__ can be vectors themselves, to be concatenated into a single vector.
        * The scalar element __ak__ of a vector __X__ is retrieved by '`X[k]`'.
        * The sub-vector __\[ X\[p\]...X\[p+q-1\] \]__ (of size __q__) of a vector __X__ is retrieved by '`X[p,q]`'.
        * Equality/inequality comparisons between two vectors is done with operators '`==`' and '`!=`'.
        * Some vector-specific functions can be used on vector values: '`cross(X,Y)`' (cross product), '`dot(X,Y)`' (dot product), '`size(X)`' (vector dimension), '`sort(X,_is_increasing,_chunk_size)`' (sorting values), '`reverse(A)`' (reverse order of components), '`shift(A,_length,_boundary_conditions)`' and '`same(A,B,_nb_vals,_is_case_sensitive)`' (vector equality test).
        * Function '`normP(u1,...,un)`' computes the LP-norm of the specified vector (`P` being an __unsigned integer__ constant or __'inf__'). If `P` is omitted, the L2 norm is used.
        * Function '`resize(A,size,_interpolation,_boundary_conditions)`' returns a resized version of a vector __'A'__ with specified interpolation mode. __'interpolation'__ can be __\{ -1=none (memory content) \| 0=none \| 1=nearest \| 2=average \| 3=linear \| 4=grid \| 5=bicubic \| 6=lanczos \}__, and __'boundary_conditions'__  can be __\{ 0=dirichlet \| 1=neumann \| 2=periodic \| 3=mirror \}__.
        * Function '`find(A,B,_is_forward,_starting_indice)`' returns the index where sub-vector __B__ appears in vector __A__, (or __-1__ if __B__ is not found in __A__). Argument __A__ can be also replaced by an image indice __#ind__.
        * A __2__-dimensional vector may be seen as a complex number and used in those particular functions/operators: '`**`' (complex multiplication), '`//`' (complex division), '`^^`' (complex exponentiation), '`**=`' (complex self-multiplication), '`//=`' (complex self-division), '`^^=`' (complex self-exponentiation), '`cabs()`' (complex modulus), '`carg()`' (complex argument), '`cconj()`' (complex conjugate), '`cexp()`' (complex exponential) and '`clog()`' (complex logarithm).
        * A __MN__-dimensional vector may be seen as a __M__ x __N__ matrix and used in those particular functions/operators: '`*`' (matrix-vector multiplication), '`det(A)`' (determinant), '`diag(V)`' (diagonal matrix from a vector), '`eig(A)`' (eigenvalues/eigenvectors), '`eye(n)`' (n x n identity matrix), '`inv(A)`' (matrix inverse), '`mul(A,B,_nb_colsB)`' (matrix-matrix multiplication), '`pseudoinv(A,_nb_colsA)`', '`rot(u,v,w,angle)`' (3d rotation matrix), '`rot(angle)`' (2d rotation matrix), '`solve(A,B,_nb_colsB)`' (least-square solver of linear system A.X = B), '`svd(A,_nb_colsA)`' (singular value decomposition), '`trace(A)`' (matrix trace) and '`transp(A,nb_colsA)`' (matrix transpose). Argument '`nb_colsB`' may be omitted if it is equal to __1__.
        * Specifying a vector-valued math expression as an argument of a command that operates on image values (e.g. '`fill`') modifies the whole spectrum range of the processed image(s), for each spatial coordinates __(x,y,z)__. The command does not loop over the __C__-axis in this case.
    + __String manipulation:__ Character strings are defined and managed as vectors objects.
       Dedicated functions and initializers to manage strings are
        * `[ 'string' ]` and `'string'` define a vector whose values are the ascii codes of the specified __character string__ (e.g. `'foo'` is equal to __\[ 102,111,111 \]__).
        * `_'character'` returns the (scalar) ascii code of the specified character (e.g. `_'A'` is equal to __65__).
        * A special case happens for __empty__ strings: Values of both expressions `[ '' ]` and `''` are __0__.
        * Functions '`lowercase()`' and '`uppercase()`' return string with all string characters lowercased or uppercased.
        * Function '`stov(str,_starting_indice,_is_strict)`' parses specified string '`str`' and returns the value contained in it.
        * Function '`vtos(expr,_nb_digits,_siz)`' returns a vector of size '`siz`' which contains the ascii representation of values described by expression '`expr`'. '`nb_digits`' can be __\{ -1=auto-reduced \| 0=all \| >0=max number of digits \}__.
        * Function '`echo(str1,str2,...,strN)`' prints the concatenation of given string arguments on the console.
        * Function '`cats(str1,str2,...,strN,siz)`' returns the concatenation of given string arguments as a new vector of size '`siz`'.
    + __Special operators__ can be used:
        * '`;`': expression separator. The returned value is always the last encountered expression. For instance expression '`1;2;pi`' is evaluated as '`pi`'.
        * '`=`': variable assignment. Variables in mathematical parser can only refer to numerical values (vectors or scalars). Variable names are case-sensitive. Use this operator in conjunction with '`;`' to define more complex evaluable expressions, such as '`t=cos(x);3*t^2+2*t+1`'. These variables remain __local__ to the mathematical parser and cannot be accessed outside  the evaluated expression.
        * Variables defined in math parser may have a __constant__ property, by specifying keyword `const` before the variable name (e.g. `const foo = pi/4;`). The value set to such a variable must be indeed a `constant scalar`. Constant variables allows certain types of optimizations in the math JIT compiler.
    + The following __specific functions__ are also defined:
        * '`u(max)`' or '`u(min,max)`': return a random value between __\[0,max\]__ or __\[min,max\]__, following a uniform distribution.
        * '`i(_a,_b,_c,_d,_interpolation_type,_boundary_conditions)`': return the value of the pixel located at position __(a,b,c,d)__ in the associated image, if any (__0__ otherwise). __'interpolation_type'__ can be __\{ 0=nearest neighbor \| other=linear \}__. __'boundary_conditions'__ can be __\{ 0=dirichlet \| 1=neumann \| 2=periodic \| 3=mirror \}__. Omitted coordinates are replaced by their default values which are respectively `x, y, z, c, interpolation` and `boundary`. For instance command '`fill 0.5*(i(x+1)-i(x-1))`' will estimate the X-derivative of an image with a classical finite difference scheme.
        * '`j(_dx,_dy,_dz,_dc,_interpolation_type,_boundary_conditions)`' does the same for the pixel located at position __(x+dx,y+dy,z+dz,c+dc)__ (pixel access relative to the current coordinates).
        * '`i[offset,_boundary_conditions]`' returns the value of the pixel located at specified __'offset'__ in the associated image buffer (or __0__ if offset is out-of-bounds).
        * '`j[offset,_boundary_conditions]`' does the same for an offset relative to the current pixel coordinates __(x,y,z,c)__.
        * '`i(#ind,_x,_y,_z,_c,_interpolation,_boundary_conditions)`', '`j(#ind,_dx,_dy,_dz,_dc,_interpolation,_boundary_conditions)`', '`i[#ind,offset,_boundary_conditions]`' and '`i[offset,_boundary_conditions]`' are similar expressions used to access pixel values for any numbered image __\[ind\]__ of the list.
        * '`I/J[offset,_boundary_conditions]`' and '`I/J(#ind,_x,_y,_z,_interpolation,_boundary_conditions)`' do the same as '`i/j[offset,_boundary_conditions]`' and '`i/j(#ind,_x,_y,_z,_c,_interpolation,_boundary_conditions)`' but return a vector instead of a scalar (e.g. a vector __\[ R,G,B \]__ for a pixel at __(a,b,c)__ in a color image).
        * '`sort(#ind,_is_increasing,_axis)`' sorts the values in the specified image __\[ind\]__.
        * '`crop(_#ind,_x,_y,_z,_c,_dx,_dy,_dz,_dc,_boundary_conditions)`' returns a vector whose values come from the cropped region of image __\[ind\]__ (or from default image selected if '`ind`' is not specified). Cropped region starts from point __(x,y,z,c)__ and has a size of __dx x dy x dz x dc__. Arguments for coordinates and sizes can be omitted if they are not ambiguous (e.g. '`crop(#ind,x,y,dx,dy)`' is a valid invocation of this function).
        * '`draw(_#ind,S,x,y,z,c,dx,_dy,_dz,_dc,_opacity,_M,_max_M)`' draws a sprite __S__ in image __\[ind\]__ (or in default image selected if '`ind`' is not specified) at coordinates __(x,y,z,c)__. The size of the sprite __dx x dy x dz x dc__ must be specified. You can also specify a corresponding opacity mask __M__ if its size matches __S__.
        * '`resize(#ind,w,_h,_d,_s,_interp,_boundary_conditions,cx,_cy,_cz,_cc)`' resizes an image of the associated list with specified dimension and interpolation method. When using this, function, you should consider retrieving the (non-constant) image dimensions using the dynamic functions '`w(_#ind)`', '`h(_#ind)`', '`d(_#ind)`', '`s(_#ind)`', '`wh(_#ind)`', '`whd(_#ind)`' and '`whds(_#ind)`' instead of the corresponding constant variables.
        * '`if(condition,expr_then,_expr_else)`': return value of '`expr_then`' or '`expr_else`', depending on the value of '`condition`' __(0=false, other=true)__. '`expr_else`' can be omitted in which case __0__ is returned if the condition does not hold. Using the ternary operator '`condition?expr_then[:expr_else]`' gives an equivalent expression. For instance, expressions '`if(x%10==0,255,i)`' and '`x%10?i:255`' both draw blank vertical lines on every 10th column of an image.
        * '`dowhile(expression,_condition)`' repeats the evaluation of '`expression`' until '`condition`' vanishes (or until '`expression`' vanishes if no '`condition`' is specified). For instance, the expression: '`if(N<2,N,n=N-1;F0=0;F1=1;dowhile(F2=F0+F1;F0=F1;F1=F2,n=n-1))`' returns the Nth value of the Fibonacci sequence, for __N>=0__ (e.g., __46368__ for __N=24__). '`dowhile(expression,condition)`' always evaluates the specified expression at least once, then check for the loop condition. When done, it returns the last value of '`expression`'.
        * '`for(init,condition,_procedure,body)`' first evaluates the expression '`init`', then iteratively evaluates '`body`' (followed by '`procedure`' if specified) while '`condition`' is verified (i.e. not zero). It may happen that no iteration is done, in which case the function returns __nan__. Otherwise, it returns the last value of '`body`'. For instance, the expression: '`if(N<2,N,for(n=N;F0=0;F1=1,n=n-1,F2=F0+F1;F0=F1;F1=F2))`' returns the __Nth__ value of the Fibonacci sequence, for __N>=0__ (e.g., __46368__ for __N=24__).
        * '`whiledo(condition,expression)`' is exactly the same as '`for(init,condition,expression)`' without the specification of an initializing expression.
        * '`break()`' and '`continue()`' respectively breaks and continues the current running bloc (loop, init or main environment).
        * '`date(attr,path)`' returns the date attribute for the given 'path' (file or directory), with __'attr'__ being __\{ 0=year \| 1=month \| 2=day \| 3=day of week \| 4=hour \| 5=minute \| 6=second \}__, or a vector of those values.
        * '`date(_attr)` returns the specified attribute for the current (locale) date.
        * '`print(expr1,expr2,...)` or '`print(#ind)` prints the value of the specified expressions (or image information) on the console, and returns the value of the last expression (or __nan__ in case of an image). Function '`prints(expr)`' also prints the string composed of the ascii characters defined by the vector-valued expression (e.g. '`prints('Hello')`').
        * '`debug(expression)` prints detailed debug information about the sequence of operations done by the math parser to evaluate the expression (and returns its value).
        * '`display(_X,_w,_h,_d,_s)` or '`display(#ind)` display the contents of the vector '`X`' (or specified image) and wait for user events. if no arguments are provided, a memory snapshot of the math parser environment is displayed instead.
        * '`init(expression)` and '`end(expression)` evaluates the specified expressions only once, respectively at the beginning and end of the evaluation procedure, and this, even when multiple evaluations are required (e.g. in '`fill init(foo=0);++foo`').
        * '`copy(dest,src,_nb_elts,_inc_d,_inc_s,_opacity)` copies an entire memory block of '`nb_elts`' elements starting from a source value '`src`' to a specified destination '`dest`', with increments defined by '`inc_d`' and '`inc_s`' respectively for the destination and source pointers.
        * '`stats(_#ind)` returns the statistics vector of the running image __\[ind\]__, i.e the vector __\[ im,iM,ia,iv,xm,ym,zm,cm,xM,yM,zM,cM,is,ip \]__ (14 values).
        * '`unref(a,b,...)` destroys references to the named variable given as arguments.
        * '`_(expr)` just ignores its arguments (mainly useful for debugging).
    + __User-defined macros:__
        * Custom macro functions can be defined in a math expression, using the assignment operator '`=`', e.g. '`foo(x,y) = cos(x + y); result = foo(1,2) + foo(2,3)`'.
        * Trying to override a built-in function (e.g. '`abs()`') has no effect.
        * Overloading macros with different number of arguments is possible. Re-defining a previously defined macro with the same number of arguments discards its previous definition.
        * Macro functions are indeed processed as __macros__ by the mathematical evaluator. You should avoid invoking them with arguments that are themselves results of assignments or self-operations. For instance, '`foo(x) = x + x; z = 0; foo(++z)`' returns __'4'__ rather than expected value __'2'__.
        * When substituted, macro arguments are placed inside parentheses, except if a number sign '`#`' is located just before or after the argument name. For instance, expression '`foo(x,y) = x*y; foo(1+2,3)`' returns __'9'__ (being substituted as '`(1+2)*(3)`'), while expression '`foo(x,y) = x#*y#; foo(1+2,3)`' returns __'7'__ (being substituted as '`1+2*3`').
        * Number signs appearing between macro arguments function actually count for '`empty`' separators. They may be used to force the substitution of macro arguments in unusual places, e.g. as in '`str(N) = ['I like N#'];`'.
    + __Multi-threaded__ and __in-place__ evaluation:
        * If your image data are large enough and you have several CPUs available, it is likely that the math expression passed to a '`fill`' or '`input`' command is evaluated in parallel, using multiple computation threads.
        * Starting an expression with '`:`' or '`*`' forces the evaluations required for an image to be run in parallel, even if the amount of data to process is small (beware, it may be slower to evaluate in this case!). Specify '`:`' (instead of '`*`') to avoid possible image copy done before evaluating the expression (this saves memory, but do this only if you are sure this step is not required!)
        * If the specified expression starts with '`>`' or '`<`', the pixel access operators '`i()`', '`i[]`', '`j()`' and '`j[]`' return values of the image being currently modified, in forward ('`>`') or backward ('`<`') order. The multi-threading evaluation of the expression is also disabled in this case.
        * Function '`critical(operands)`' forces the execution of the given operands in a single thread at a time.
    + Expressions '`i(_#ind,x,_y,_z,_c)=value`', '`j(_#ind,x,_y,_z,_c)=value`', '`i[_#ind,offset]=value`' and '`j[_#ind,offset]=value`' set a pixel value at a different location than the running one in the image __[ind]__ (or in the associated image if argument '`#ind`' is omitted), either with global coordinates/offsets (with '`i(...)`' and '`i[...]`'), or relatively to the current position __(x,y,z,c)__ (with '`j(...)`' and '`j[...]`'). These expressions always return '`value`'.
