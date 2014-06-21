#include "Premult.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getPremultPluginID(ids);
            getUnpremultPluginID(ids);
        }
    }
}
