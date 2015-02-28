#include "TestRender.h"
#include "TestPosition.h"
#include "TestGroups.h"

#include "ofxsImageEffect.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getTestRenderPluginID(ids);
            getTestPositionPluginID(ids);
            getTestGroupsPluginID(ids);
        }
    }
}
