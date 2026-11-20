# Changelog

Changes that users can see, in particular the output changes that library updates may bring
without a major version bump of the plug-ins (Natron 2.6 policy).

## Unreleased (Natron 2.6)

### CImg 4.0.8

CImg is updated from 4.0.7 to 4.0.8, patched as openfx-gmic's (#199). Output changes, in
ExpressionCImg unless noted:

- `var(a,b)` and `std(a,b)` with two arguments now give the population variance and standard
  deviation, as with three or more arguments: ½ and 1/√2 of the old values.
- `log(1+x)` (x a variable) is now computed with `log1p(x)`: more accurate for small |x|.
- `log1p()` is now a built-in function, so a user macro named `log1p` is ignored.
- `x^-1` is now compiled to `1/x` (last-bit differences).
- `vvar()` and `vstd()` with three or more arguments are now computed with CImg's `variance()`:
  last-bit differences.
- `svd()`: computed in double; singular vectors may change sign (e.g. non-square matrices), last-bit differences.
- MSVC builds only: `hypot(inf,inf)` is NaN ([CImg#503]).

Fixes, with openfx-gmic's patches of CImg 4.0.8 ([CImg#501], [CImg#502]):

- `vvar(a,b)` no longer crashes.
- `vargkth(a,b)` returns 0 again, as in 4.0.7.
- `log(y)` no longer overwrites a variable `y` computed as `1+x`.
- `abs()` no longer overwrites a variable assigned in its argument (a bug of 4.0.7 too).

[CImg#501]: https://github.com/GreycLab/CImg/issues/501
[CImg#502]: https://github.com/GreycLab/CImg/issues/502
[CImg#503]: https://github.com/GreycLab/CImg/issues/503
