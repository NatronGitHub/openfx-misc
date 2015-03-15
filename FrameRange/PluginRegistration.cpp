#include "FrameRange.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getFrameRangePluginID(ids);
        }
    }
}
