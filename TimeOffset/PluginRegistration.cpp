#include "TimeOffset.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getTimeOffsetPluginID(ids);
        }
    }
}
