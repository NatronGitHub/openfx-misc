#include "Anaglyph.h"
#include "ChromaKeyer.h"
#include "ColorCorrect.h"
#include "CornerPin.h"
#include "Grade.h"
#include "JoinViews.h"
#include "Merge.h"
#include "MixViews.h"
#include "OneView.h"
#include "RGBLut.h"
#include "Roto.h"
#include "SideBySide.h"
#include "Switch.h"
#include "TimeOffset.h"
#include "Transform.h"
#include "Crop.h"
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
            static ColorCorrectPluginFactory p9("net.sf.openfx:ColorCorrectPlugin", 1, 0);
            ids.push_back(&p9);
            static GradePluginFactory p10("net.sf.openfx:GradePlugin", 1, 0);
            ids.push_back(&p10);
            static TransformPluginFactory p11("net.sf.openfx:TransformPlugin", 1, 0);
            ids.push_back(&p11);
            static TransformMaskedPluginFactory p12("net.sf.openfx:TransformMaskedPlugin", 1, 0);
            ids.push_back(&p12);
            static MergePluginFactory p13("net.sf.openfx:MergePlugin", 1, 0);
            ids.push_back(&p13);
            static ChromaKeyerPluginFactory p14("net.sf.openfx:ChromaKeyerPlugin", 1, 0);
            ids.push_back(&p14);
            static RotoPluginFactory p15("net.sf.openfx:RotoPlugin", 1, 0);
            ids.push_back(&p15);
            static CornerPinPluginFactory p16("net.sf.openfx:CornerPinPlugin",1,0);
            ids.push_back(&p16);
            static CornerPinMaskedPluginFactory p17("net.sf.openfx:CornerPinMaskedPlugin", 1, 0);
            ids.push_back(&p17);
            static CropPluginFactory p18("net.sf.openfx:CropPlugin", 1, 0);
            ids.push_back(&p18);
           // ReConverge is not finished/tested
            //static ReConvergePluginFactory p("net.sf.openfx:reConvergePlugin", 1, 0);
            //ids.push_back(&p);
        }
    }
}
