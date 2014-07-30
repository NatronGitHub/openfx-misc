#include "Anaglyph.h"
#include "ChromaKeyer.h"
#include "ColorCorrect.h"
#include "Constant.h"
#include "CopyRectangle.h"
#include "CornerPin.h"
#include "Crop.h"
#include "Difference.h"
#include "Grade.h"
#include "Invert.h"
#include "JoinViews.h"
#include "Merge.h"
#include "MixViews.h"
#include "Noise.h"
#include "NoOp.h"
#include "OneView.h"
#include "Premult.h"
#include "ReConverge.h"
#include "RGBLut.h"
#include "Roto.h"
#include "Shuffle.h"
#include "SideBySide.h"
#include "Switch.h"
#include "TimeOffset.h"
#include "TrackerPM.h"
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
            getCopyRectanglePluginID(ids);
            getCornerPinPluginIDs(ids);
            getCropPluginID(ids);
            getDifferencePluginID(ids);
            getGradePluginID(ids);
            getInvertPluginID(ids);
            getJoinViewsPluginID(ids);
            getMergePluginID(ids);
            getMixViewsPluginID(ids);
            getNoisePluginID(ids);
            getNoOpPluginID(ids);
            getOneViewPluginID(ids);
            getPremultPluginIDs(ids);
            getReConvergePluginID(ids);
            getRGBLutPluginID(ids);
            getRotoPluginID(ids);
            getShufflePluginID(ids);
            getSideBySidePluginID(ids);
            getSwitchPluginID(ids);
            getTimeOffsetPluginID(ids);
            getTrackerPMPluginID(ids);
            getTransformPluginIDs(ids);
        }
    }
}
