#include "CImgPlasma.h"

#include "ofxsImageEffect.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getCImgPlasmaPluginID(ids);
        }
    }
}
