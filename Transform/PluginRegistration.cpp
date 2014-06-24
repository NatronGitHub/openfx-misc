#include "Transform.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getTransformPluginIDs(ids);
        }
    }
}
