#include "CornerPin.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getCornerPinPluginID(ids);
            getCornerPinMaskedPluginID(ids);
        }
    }
}
