#include "Grade.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static GradePluginFactory p("net.sf.openfx:GradePlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
