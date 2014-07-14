#include "Noise.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getNoisePluginID(ids);
        }
    }
}
