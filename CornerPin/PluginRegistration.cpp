#include "CornerPin.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static CornerPinPluginFactory p("net.sf.openfx:CornerPinPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
