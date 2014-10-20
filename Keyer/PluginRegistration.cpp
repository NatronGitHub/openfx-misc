#include "Keyer.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getKeyerPluginID(ids);
        }
    }
}
