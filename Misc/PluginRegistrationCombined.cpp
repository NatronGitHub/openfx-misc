#include "Switch.h"
#include "TimeOffset.h"
#include "RGBLut.h"
#include "SideBySide.h"
#include "MixViews.h"
#include "OneView.h"
#include "JoinViews.h"
#include "Anaglyph.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static SwitchPluginFactory p1("net.sf.openfx:switchPlugin", 1, 0);
            ids.push_back(&p1);
            static TimeOffsetPluginFactory p2("net.sf.openfx:timeOffset", 1, 0);
            ids.push_back(&p2);
            static RGBLutPluginFactory p3("net.sf.openfx:RGBLutPlugin", 1, 0);
            ids.push_back(&p3);
            static SideBySidePluginFactory p4("net.sf.openfx:sideBySidePlugin", 1, 0);
            ids.push_back(&p4);
            static MixViewsPluginFactory p5("net.sf.openfx:mixViewsPlugin", 1, 0);
            ids.push_back(&p5);
            static OneViewPluginFactory p6("net.sf.openfx:oneViewPlugin", 1, 0);
            ids.push_back(&p6);
            static JoinViewsPluginFactory p7("net.sf.openfx:joinViewsPlugin", 1, 0);
            ids.push_back(&p7);
            static AnaglyphPluginFactory p8("net.sf.openfx:anaglyphPlugin", 1, 0);
            ids.push_back(&p8);
            // ReConverge is not finished/tested
            //static ReConvergePluginFactory p("net.sf.openfx:reConvergePlugin", 1, 0);
            //ids.push_back(&p);
        }
    }
}
