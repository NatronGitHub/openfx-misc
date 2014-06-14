#include "ofxsImageEffect.h"
#include "JoinViews.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getJoinViewsPluginID(ids);
        }
    }
}
