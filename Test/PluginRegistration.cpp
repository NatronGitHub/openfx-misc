#include "TestRender.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getTestRenderPluginID(ids);
        }
    }
}
