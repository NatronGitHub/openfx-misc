#include "Position.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getPositionPluginID(ids);
        }
    }
}
