#include "Anaglyph.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static AnaglyphPluginFactory p("net.sf.openfx:anaglyphPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
