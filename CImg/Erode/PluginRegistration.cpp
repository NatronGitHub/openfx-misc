#include "CImgDilate.h"
#include "CImgErode.h"

#include "ofxsImageEffect.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getCImgDilatePluginID(ids);
            getCImgErodePluginID(ids);
        }
    }
}
