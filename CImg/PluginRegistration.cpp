#include "CImgBlur.h"
#include "CImgBilateral.h"
#include "CImgDilate.h"
#include "CImgErode.h"
#include "CImgSmooth.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getCImgBlurPluginID(ids);
            getCImgBilateralPluginID(ids);
            getCImgDilatePluginID(ids);
            getCImgErodePluginID(ids);
            getCImgSmoothPluginID(ids);
        }
    }
}
