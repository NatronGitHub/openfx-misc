#include "Shuffle.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getShufflePluginID(ids);
        }
    }
}
