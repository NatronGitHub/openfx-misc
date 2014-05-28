#include "Roto.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static RotoPluginFactory p("net.sf.openfx:RotoPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
