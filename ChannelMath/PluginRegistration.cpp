#include "ChannelMath.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getChannelMathPluginID(ids);
        }
    }
}
