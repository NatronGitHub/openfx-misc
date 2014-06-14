#include "Anaglyph.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getAnaglyphPluginID(ids);
        }
    }
}
