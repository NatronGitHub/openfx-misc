#include "OneView.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static OneViewPluginFactory p("net.sf.openfx:oneViewPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
