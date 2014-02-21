#include "ReConverge.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static ReConvergePluginFactory p("net.sf.openfx:reConvergePlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
