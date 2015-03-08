#include "IDistort.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getIDistortPluginID(ids);
        }
    }
}
