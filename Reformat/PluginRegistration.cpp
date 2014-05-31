#include "Reformat.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static ReformatPluginFactory p("net.sf.openfx:ReformatPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
