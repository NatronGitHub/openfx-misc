# Changelog

Changes that users can see, in particular the output changes that library updates may bring
without a major version bump of the plug-ins (Natron 2.6 policy).

## Unreleased (Natron 2.6)

### CImg 4.0.8

CImg is updated from 4.0.7 to 4.0.8, patched as openfx-gmic's (#199). Output changes, in
ExpressionCImg unless noted:

- `var(a,b)` and `std(a,b)` with two arguments now give the population variance and standard
  deviation, as with three or more arguments: ½ and 1/√2 of the old values.
- `log(1+x)` is now computed with `log1p(x)`: last-bit differences, visible for |x| < 1e-7.
- `log1p()` is now a built-in function, so a user macro named `log1p` is ignored.
- `x^-1` is now compiled to `1/x` (last-bit differences).
- `vvar()` and `vstd()` with three or more arguments are now computed with CImg's `variance()`:
  last-bit differences.
- MSVC builds only: `hypot(inf,inf)` is now NaN (it was inf).
- Two CImg 4.0.8 regressions (`vvar(a,b)` crashes; `log(y)` with `y=1+x` overwrites `y`): fixes
  pending openfx-gmic patches.
