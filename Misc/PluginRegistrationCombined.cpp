#include "Add.h"
#include "AdjustRoD.h"
#include "Anaglyph.h"
#include "CheckerBoard.h"
#include "ChromaKeyer.h"
#include "Clamp.h"
#include "ClipTest.h"
#include "ColorCorrect.h"
#include "ColorMatrix.h"
#include "ColorTransform.h"
#include "Constant.h"
#include "CopyRectangle.h"
#include "CornerPin.h"
#include "Crop.h"
#include "Deinterlace.h"
#include "Difference.h"
#include "Dissolve.h"
#include "FrameBlend.h"
#include "Gamma.h"
#include "GodRays.h"
#include "Grade.h"
#include "HSVTool.h"
#include "ImageStatistics.h"
#include "Invert.h"
#include "JoinViews.h"
#include "Keyer.h"
#include "Merge.h"
#include "MixViews.h"
#include "Multiply.h"
#include "Noise.h"
#include "NoOp.h"
#include "OneView.h"
#include "Premult.h"
#include "Radial.h"
#include "Ramp.h"
#include "ReConverge.h"
#include "Rectangle.h"
#include "Retime.h"
#include "ColorLookup.h"
#include "Roto.h"
#include "Saturation.h"
#include "Shuffle.h"
#include "SideBySide.h"
#include "Switch.h"
#include "TestRender.h"
#include "TimeOffset.h"
#include "TrackerPM.h"
#include "Transform.h"
#include "VectorToColor.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getAddPluginID(ids);
            getAdjustRoDPluginID(ids);
            getAnaglyphPluginID(ids);
            getCheckerBoardPluginID(ids);
            getChromaKeyerPluginID(ids);
            getClampPluginID(ids);
            getClipTestPluginID(ids);
            getColorCorrectPluginID(ids);
            getColorMatrixPluginID(ids);
            getColorTransformPluginIDs(ids);
            getConstantPluginID(ids);
            getCopyRectanglePluginID(ids);
            getCornerPinPluginIDs(ids);
            getCropPluginID(ids);
            getDeinterlacePluginID(ids);
            getDifferencePluginID(ids);
            getDissolvePluginID(ids);
            getFrameBlendPluginID(ids);
            getGammaPluginID(ids);
            getGodRaysPluginID(ids);
            getGradePluginID(ids);
            getHSVToolPluginID(ids);
            getImageStatisticsPluginID(ids);
            getInvertPluginID(ids);
            getJoinViewsPluginID(ids);
            getKeyerPluginID(ids);
            getMergePluginID(ids);
            getMixViewsPluginID(ids);
            getMultiplyPluginID(ids);
            getNoisePluginID(ids);
            getNoOpPluginID(ids);
            getOneViewPluginID(ids);
            getRadialPluginID(ids);
            getRampPluginID(ids);
            getPremultPluginIDs(ids);
#ifdef DEBUG
            getReConvergePluginID(ids);
#endif
            getRectanglePluginID(ids);
            getRetimePluginID(ids);
            getColorLookupPluginID(ids);
            getRotoPluginID(ids);
            getSaturationPluginID(ids);
            getShufflePluginID(ids);
            getSideBySidePluginID(ids);
            getSwitchPluginID(ids);
#ifdef DEBUG
            getTestRenderPluginID(ids);
#endif
            getTimeOffsetPluginID(ids);
            getTrackerPMPluginID(ids);
            getTransformPluginIDs(ids);
            getVectorToColorPluginID(ids);
        }
    }
}
