#include "ColorCorrect.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static ColorCorrectPluginFactory p("net.sf.openfx:ColorCorrectPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
