#include "ReConverge.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
#ifdef DEBUG
            getReConvergePluginID(ids);
#endif
        }
    }
}
