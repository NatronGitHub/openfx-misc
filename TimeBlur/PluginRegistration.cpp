#include "TimeBlur.h"
#include "NoTimeBlur.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getTimeBlurPluginID(ids);
            getNoTimeBlurPluginID(ids);
        }
    }
}
