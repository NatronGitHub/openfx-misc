#include "TimeOffset.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static TimeOffsetPluginFactory p("net.sf.openfx:timeOffset", 1, 0);
            ids.push_back(&p);
        }
    }
}
