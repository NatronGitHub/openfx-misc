#include "ofxsImageEffect.h"
#include "MixViews.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static MixViewsPluginFactory p("net.sf.openfx:mixViewsPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
