#include "CImgBlur.h"
#include "CImgBilateral.h"
#include "CImgDenoise.h"
#include "CImgDilate.h"
#include "CImgEqualize.h"
#include "CImgErode.h"
#include "CImgNoise.h"
#include "CImgPlasma.h"
#include "CImgSmooth.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getCImgBlurPluginID(ids);
            getCImgBilateralPluginID(ids);
            getCImgDenoisePluginID(ids);
            getCImgDilatePluginID(ids);
            getCImgEqualizePluginID(ids);
            getCImgErodePluginID(ids);
            getCImgNoisePluginID(ids);
            getCImgPlasmaPluginID(ids);
            getCImgSmoothPluginID(ids);
        }
    }
}
