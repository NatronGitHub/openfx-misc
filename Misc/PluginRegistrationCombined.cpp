#include "AdjustRoD.h"
#include "Anaglyph.h"
#include "ChromaKeyer.h"
#include "Clamp.h"
#include "ColorCorrect.h"
#include "ColorMatrix.h"
#include "Constant.h"
#include "CopyRectangle.h"
#include "CornerPin.h"
#include "Crop.h"
#include "Deinterlace.h"
#include "Difference.h"
#include "Dissolve.h"
#include "Grade.h"
#include "Invert.h"
#include "JoinViews.h"
#include "Keyer.h"
#include "Merge.h"
#include "MixViews.h"
#include "Noise.h"
#include "NoOp.h"
#include "OneView.h"
#include "Premult.h"
#include "Ramp.h"
#ifdef DEBUG
#include "ReConverge.h"
#endif
#include "Retime.h"
#include "ColorLookup.h"
#include "Roto.h"
#include "Shuffle.h"
#include "SideBySide.h"
#include "Switch.h"
#include "TestRender.h"
#include "TimeOffset.h"
#include "TrackerPM.h"
#include "Transform.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getAdjustRoDPluginID(ids);
            getAnaglyphPluginID(ids);
            getChromaKeyerPluginID(ids);
            getClampPluginID(ids);
            getColorCorrectPluginID(ids);
            getColorMatrixPluginID(ids);
            getConstantPluginID(ids);
            getCopyRectanglePluginID(ids);
            getCornerPinPluginIDs(ids);
            getCropPluginID(ids);
            getDeinterlacePluginID(ids);
            getDifferencePluginID(ids);
            getDissolvePluginID(ids);
            getGradePluginID(ids);
            getInvertPluginID(ids);
            getJoinViewsPluginID(ids);
            getKeyerPluginID(ids);
            getMergePluginID(ids);
            getMixViewsPluginID(ids);
            getNoisePluginID(ids);
            getNoOpPluginID(ids);
            getOneViewPluginID(ids);
            getRampPluginID(ids);
            getPremultPluginIDs(ids);
#ifdef DEBUG
            getReConvergePluginID(ids);
#endif
            getRetimePluginID(ids);
            getColorLookupPluginID(ids);
            getRotoPluginID(ids);
            getShufflePluginID(ids);
            getSideBySidePluginID(ids);
            getSwitchPluginID(ids);
#ifdef DEBUG
            getTestRenderPluginID(ids);
#endif
            getTimeOffsetPluginID(ids);
            getTrackerPMPluginID(ids);
            getTransformPluginIDs(ids);
        }
    }
}
