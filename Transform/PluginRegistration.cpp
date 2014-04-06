#include "Transform.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static TransformPluginFactory p("net.sf.openfx:TransformPlugin", 1, 0);
            ids.push_back(&p);
            static TransformMaskedPluginFactory p("net.sf.openfx:TransformMaskedPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
