#include "TestRender.h"
#include "TestPosition.h"
#include "TestGroups.h"
#ifdef OFX_SUPPORTS_OPENGLRENDER
#include "TestOpenGL.h"
#endif

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
#ifdef OFX_SUPPORTS_OPENGLRENDER
            getTestOpenGLPluginID(ids);
#endif
        }
    }
}
