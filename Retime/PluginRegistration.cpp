#include "Retime.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getRetimePluginID(ids);
        }
    }
}
