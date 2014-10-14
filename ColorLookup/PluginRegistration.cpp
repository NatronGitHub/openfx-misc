#include "ColorLookup.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getColorLookupPluginID(ids);
        }
    }
}
