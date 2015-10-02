#include "MaskableFilter.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getMaskableFilterPluginID(ids);
        }
    }
}
