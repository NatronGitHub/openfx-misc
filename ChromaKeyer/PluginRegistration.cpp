#include "ChromaKeyer.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static ChromaKeyerPluginFactory p("net.sf.openfx:ChromaKeyerPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
