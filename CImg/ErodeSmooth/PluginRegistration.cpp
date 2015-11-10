#include "CImgErodeSmooth.h"

#include "ofxsImageEffect.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getCImgErodeSmoothPluginID(ids);
        }
    }
}
