#include "Shuffle.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static ShufflePluginFactory p("net.sf.openfx:ShufflePlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
