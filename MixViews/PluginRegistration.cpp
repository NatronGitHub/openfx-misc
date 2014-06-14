#include "ofxsImageEffect.h"
#include "MixViews.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getMixViewsPluginID(ids);
        }
    }
}
