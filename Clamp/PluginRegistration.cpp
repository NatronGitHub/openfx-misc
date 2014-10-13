#include "Clamp.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getClampPluginID(ids);
        }
    }
}
