#include "Add.h"
#include "AdjustRoD.h"
#include "Anaglyph.h"
#include "AppendClip.h"
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
#include "FrameHold.h"
#include "FrameRange.h"
#include "Gamma.h"
#include "GodRays.h"
#include "Grade.h"
#include "HSVTool.h"
#include "Distortion.h"
#include "ImageStatistics.h"
#include "Invert.h"
#include "JoinViews.h"
#include "Keyer.h"
#include "MatteMonitor.h"
#include "Merge.h"
#include "Mirror.h"
#include "MixViews.h"
#include "Multiply.h"
#include "NoOp.h"
#include "OneView.h"
#include "Position.h"
#include "Premult.h"
#include "Radial.h"
#include "Ramp.h"
#include "Rand.h"
#include "ReConverge.h"
#include "Rectangle.h"
#include "Retime.h"
#include "ColorLookup.h"
#include "Roto.h"
#include "Saturation.h"
#include "Shuffle.h"
#include "SideBySide.h"
#include "SlitScan.h"
#include "Switch.h"
#include "TimeBlur.h"
#include "NoTimeBlur.h"
#include "TimeDissolve.h"
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
            getAppendClipPluginID(ids);
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
            getFrameHoldPluginID(ids);
            getFrameRangePluginID(ids);
            getGammaPluginID(ids);
            getGodRaysPluginID(ids);
            getGradePluginID(ids);
            getHSVToolPluginID(ids);
            getDistortionPluginIDs(ids);
            getImageStatisticsPluginID(ids);
            getInvertPluginID(ids);
            getJoinViewsPluginID(ids);
            getKeyerPluginID(ids);
            getMatteMonitorPluginID(ids);
            getMergePluginID(ids);
            getMirrorPluginID(ids);
            getMixViewsPluginID(ids);
            getMultiplyPluginID(ids);
            getNoOpPluginID(ids);
            getOneViewPluginID(ids);
            getPositionPluginID(ids);
            getPremultPluginIDs(ids);
            getRadialPluginID(ids);
            getRampPluginID(ids);
            getRandPluginID(ids);
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
            getSlitScanPluginID(ids);
            getSwitchPluginID(ids);
            getTimeBlurPluginID(ids);
            getNoTimeBlurPluginID(ids);
            getTimeDissolvePluginID(ids);
            getTimeOffsetPluginID(ids);
            getTrackerPMPluginID(ids);
            getTransformPluginIDs(ids);
            getVectorToColorPluginID(ids);
        }
    }
}
