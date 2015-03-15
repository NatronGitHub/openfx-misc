#include "FrameHold.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getFrameHoldPluginID(ids);
        }
    }
}
