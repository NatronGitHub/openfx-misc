#include "Grade.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static GradePluginFactory p("fr.INRIA.openfx:TransformPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
