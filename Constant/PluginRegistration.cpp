#include "Constant.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static ConstantPluginFactory p("net.sf.openfx:ConstantPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
