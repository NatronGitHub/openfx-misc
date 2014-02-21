#include "Switch.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static SwitchPluginFactory p("net.sf.openfx:switchPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
