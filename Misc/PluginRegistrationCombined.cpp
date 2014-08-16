#include "Anaglyph.h"
#include "ChromaKeyer.h"
#include "ColorCorrect.h"
#include "ColorMatrix.h"
#include "Constant.h"
#include "CopyRectangle.h"
#include "CornerPin.h"
#include "Crop.h"
#include "Deinterlace.h"
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
#ifdef DEBUG
#include "ReConverge.h"
#endif
#include "Retime.h"
#include "RGBLut.h"
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
            getAnaglyphPluginID(ids);
            getChromaKeyerPluginID(ids);
            getColorCorrectPluginID(ids);
            getColorMatrixPluginID(ids);
            getConstantPluginID(ids);
            getCopyRectanglePluginID(ids);
            getCornerPinPluginIDs(ids);
            getCropPluginID(ids);
            getDeinterlacePluginID(ids);
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
#ifdef DEBUG
            getReConvergePluginID(ids);
#endif
            getRetimePluginID(ids);
            getRGBLutPluginID(ids);
            getRotoPluginID(ids);
            getShufflePluginID(ids);
            getSideBySidePluginID(ids);
            getSwitchPluginID(ids);
            getTestRenderPluginID(ids);
            getTimeOffsetPluginID(ids);
            getTrackerPMPluginID(ids);
            getTransformPluginIDs(ids);
        }
    }
}
