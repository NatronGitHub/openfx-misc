#include "RGBLut.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static RGBLutPluginFactory p("net.sf.openfx:RGBLutPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
