#include "CImgBlur.h"
#include "CImgBilateral.h"
#include "CImgDenoise.h"
#include "CImgDilate.h"
#include "CImgEqualize.h"
#include "CImgErode.h"
#include "CImgErodeSmooth.h"
#include "CImgExpression.h"
#include "CImgGuided.h"
#include "CImgHistEQ.h"
#include "CImgMedian.h"
#include "CImgNoise.h"
#include "CImgPlasma.h"
#include "CImgRollingGuidance.h"
#include "CImgSharpenInvDiff.h"
#include "CImgSharpenShock.h"
#include "CImgSmooth.h"

#include "ofxsImageEffect.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getCImgBlurPluginID(ids);
            getCImgBilateralPluginID(ids);
            getCImgDenoisePluginID(ids);
            getCImgDilatePluginID(ids);
            getCImgEqualizePluginID(ids);
            getCImgExpressionPluginID(ids);
            getCImgErodePluginID(ids);
            getCImgErodeSmoothPluginID(ids);
            getCImgGuidedPluginID(ids);
            getCImgHistEQPluginID(ids);
            getCImgMedianPluginID(ids);
            getCImgNoisePluginID(ids);
            getCImgPlasmaPluginID(ids);
            getCImgRollingGuidancePluginID(ids);
            getCImgSharpenInvDiffPluginID(ids);
            getCImgSharpenShockPluginID(ids);
            getCImgSmoothPluginID(ids);
        }
    }
}
