#include "CImgBlur.h"
#include "CImgSmooth.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getCImgBlurPluginID(ids);
            getCImgSmoothPluginID(ids);
        }
    }
}
