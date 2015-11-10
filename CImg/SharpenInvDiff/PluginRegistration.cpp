#include "CImgSharpenInvDiff.h"

#include "ofxsImageEffect.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getCImgSharpenInvDiffPluginID(ids);
        }
    }
}
