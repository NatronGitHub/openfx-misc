#include "ChromaKeyer.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getChromaKeyerPluginID(ids);
        }
    }
}
