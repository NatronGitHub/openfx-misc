ABOUT CHANNELMATH

ChannelMath is an OFX Plugin that allows the user to write simple
math expressions. This plugin is free to use and distribute for any purpose.

HOW TO INSTALL  CHANNELMATH

 Put ChannelMath.ofx.bundle in your OFX plugin path. This is usually:
  /Library/OFX/Plugins
  If that folder doesn't exist, you can create it. Or, if you prefer, use the environment
  variable OFX_PLUGIN_PATH.

HOW TO USE CHANNELMATH

1. For a simple grey set these values:
| field | value     |
|-------+-----------|
| expr1 | (r+g+b)/3 |
| red   | expr1     |
| blue  | expr1     |
| green | expr1     |

2. Adjust  RGB levels. Enter these values:
| field  | value        |
|--------+--------------|
| red    | r * param1.r |
| green  | g * param1.g |
| blue   | b * param1.b |
Then use the colour wheel in param1 to adjust your image.

3. Use Luminance for alpha:
| field | value                                |
|-------+--------------------------------------|
| alpha | 0.2126 * r + 0.7152 * g + 0.0722 * b |

RELEVANT WEBSITES

My homepage is http://casanico.com
The OFX plugin standard's home is http://openeffects.org

THIRD PARTY LICENSES

The libraries used by this program come from:

1. F. Deverney's openfx-misc:
https://github.com/devernay/openfx-misc
licensed under GNU GPL 2.0: http://www.gnu.org/licenses/gpl-2.0.html

2. Arash Partow's C++ mathematical expression library:
http://www.partow.net/programming/exprtk
licensed under CPL 1.0: http://opensource.org/licenses/cpl1.0.php
