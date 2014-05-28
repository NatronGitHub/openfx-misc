#include "Transform.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static TransformPluginFactory p1("net.sf.openfx:TransformPlugin", 1, 0);
            ids.push_back(&p1);
            static TransformMaskedPluginFactory p2("net.sf.openfx:TransformMaskedPlugin", 1, 0);
            ids.push_back(&p2);
        }
    }
}
