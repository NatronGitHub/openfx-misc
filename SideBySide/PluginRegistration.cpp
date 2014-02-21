#include "SideBySide.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static SideBySidePluginFactory p("net.sf.openfx:sideBySidePlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
