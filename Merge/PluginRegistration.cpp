#include "Merge.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static MergePluginFactory p("net.sf.openfx:MergePlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
