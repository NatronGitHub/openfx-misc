#include "Dissolve.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getDissolvePluginID(ids);
        }
    }
}
