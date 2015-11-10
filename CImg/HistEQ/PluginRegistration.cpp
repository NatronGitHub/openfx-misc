#include "CImgHistEQ.h"

#include "ofxsImageEffect.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getCImgHistEQPluginID(ids);
        }
    }
}
