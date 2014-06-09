#include "Difference.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static DifferencePluginFactory p("net.sf.openfx:DifferencePlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
