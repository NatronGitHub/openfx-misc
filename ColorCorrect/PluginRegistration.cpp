#include "ColorCorrect.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getColorCorrectPluginID(ids);
        }
    }
}
