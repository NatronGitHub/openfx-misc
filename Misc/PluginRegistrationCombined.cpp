#include "Anaglyph.h"
#include "ChromaKeyer.h"
#include "ColorCorrect.h"
#include "Constant.h"
#include "CornerPin.h"
#include "Crop.h"
#include "Difference.h"
#include "Grade.h"
#include "Invert.h"
#include "JoinViews.h"
#include "Merge.h"
#include "MixViews.h"
#include "OneView.h"
#include "ReConverge.h"
#include "RGBLut.h"
#include "Roto.h"
#include "Shuffle.h"
#include "SideBySide.h"
#include "Switch.h"
#include "TimeOffset.h"
#include "Transform.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getAnaglyphPluginID(ids);
            getChromaKeyerPluginID(ids);
            getColorCorrectPluginID(ids);
            getConstantPluginID(ids);
            getCornerPinPluginID(ids);
            getCornerPinMaskedPluginID(ids);
            getCropPluginID(ids);
            getDifferencePluginID(ids);
            getGradePluginID(ids);
            getInvertPluginID(ids);
            getJoinViewsPluginID(ids);
            getMergePluginID(ids);
            getMixViewsPluginID(ids);
            getOneViewPluginID(ids);
            getReConvergePluginID(ids);
            getRGBLutPluginID(ids);
            getRotoPluginID(ids);
            getShufflePluginID(ids);
            getSideBySidePluginID(ids);
            getSwitchPluginID(ids);
            getTimeOffsetPluginID(ids);
            getTransformPluginID(ids);
            getTransformMaskedPluginID(ids);
        }
    }
}
