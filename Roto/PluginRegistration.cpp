#include "Roto.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static TransformPluginFactory p("net.sf.openfx:RotoPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
