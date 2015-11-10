#include "CImgRollingGuidance.h"

#include "ofxsImageEffect.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getCImgRollingGuidancePluginID(ids);
        }
    }
}
