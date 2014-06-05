#include "Crop.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static CropPluginFactory p("net.sf.openfx:CropPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
