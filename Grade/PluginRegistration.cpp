#include "Grade.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getGradePluginID(ids);
        }
    }
}
