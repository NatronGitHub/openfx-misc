#include "Distortion.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getDistortionPluginIDs(ids);
        }
    }
}
