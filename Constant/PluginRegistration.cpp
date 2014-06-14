#include "Constant.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getConstantPluginID(ids);
        }
    }
}
