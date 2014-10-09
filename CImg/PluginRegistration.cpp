#include "CImgBlur.h"
#include "CImgBilateral.h"
#include "CImgSmooth.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getCImgBlurPluginID(ids);
            getCImgBilateralPluginID(ids);
            getCImgSmoothPluginID(ids);
        }
    }
}
