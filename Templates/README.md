This directory contains a set of generic template plugins, to be used as models.

SimpleFilter: a generic filter with per-pixel processing, channel selection, supporting multiple depths and components.

MixableFilter: same as SimpleFilter, with a "mix" parameter (use it if input and output are color).

MaskableFilter: same as MixableFilter, with a "mask" input (use it if output is in the same colorspace as input).
