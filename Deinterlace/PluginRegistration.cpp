#include "Deinterlace.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getDeinterlacePluginID(ids);
        }
    }
}
