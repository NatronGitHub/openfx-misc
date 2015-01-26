#include "ImageStatistics.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getImageStatisticsPluginID(ids);
        }
    }
}
