cmake_minimum_required(VERSION 2.8) 

project(openfx-misc)

# Check for OpenFX
if(NOT EXISTS ${PROJECT_SOURCE_DIR}/openfx/include)
    message(FATAL_ERROR "\n OpenFX is missing, please update your repository.\n")
endif()

# OfxSupport
add_subdirectory("${PROJECT_SOURCE_DIR}/openfx")
include_directories("${CMAKE_SOURCE_DIR}/openfx/Support/Plugins/include")
target_compile_definitions(OfxSupport PUBLIC OFX_EXTENSIONS_VEGAS OFX_EXTENSIONS_NUKE OFX_EXTENSIONS_TUTTLE OFX_EXTENSIONS_NATRON OFX_SUPPORTS_OPENGLRENDER)

# SupportExt
include_directories("${CMAKE_SOURCE_DIR}/SupportExt")

# OpenMP
find_package(OpenMP)
if (OPENMP_FOUND)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
endif()

# CImg
set(CIMGVERSION "02ec481605f4dd68dec9d8c5b7b4bd763ad31cac")
if(NOT EXISTS ${PROJECT_SOURCE_DIR}/CImg/CImg.h)
    execute_process(COMMAND curl https://raw.githubusercontent.com/dtschump/CImg/${CIMGVERSION}/CImg.h -o ${PROJECT_SOURCE_DIR}/CImg/CImg.h)
endif()
include_directories("${CMAKE_SOURCE_DIR}/CImg")

# Misc
include_directories("${CMAKE_SOURCE_DIR}/Misc")

# CImg.ofx (bundle)
create_ofx_plugin(CImg 
    "CImg/Bilateral;CImg/Guided;CImg/Median;CImg/SharpenInvDiff;CImg/Blur;CImg/Denoise;CImg/Erode;CImg/HistEQ;CImg/Noise;CImg/SharpenShock;CImg/Equalize;CImg/ErodeSmooth;CImg/Plasma;CImg/Smooth;CImg/Expression;CImg/RollingGuidance" 
    "" 
    "CImg/Bilateral/net.sf.cimg.CImgBilateral.png;CImg/Bilateral/net.sf.cimg.CImgBilateral.svg;CImg/Blur/net.sf.cimg.CImgBlur.png;CImg/Blur/net.sf.cimg.CImgBlur.svg;CImg/Blur/net.sf.cimg.CImgChromaBlur.png;CImg/Blur/net.sf.cimg.CImgChromaBlur.svg;CImg/Blur/net.sf.cimg.CImgBloom.png;CImg/Blur/net.sf.cimg.CImgBloom.svg;CImg/Blur/net.sf.cimg.CImgLaplacian.png;CImg/Blur/net.sf.cimg.CImgLaplacian.svg;CImg/Denoise/net.sf.cimg.CImgDenoise.png;CImg/Denoise/net.sf.cimg.CImgDenoise.svg;CImg/Equalize/net.sf.cimg.CImgEqualize.png;CImg/Equalize/net.sf.cimg.CImgEqualize.svg;CImg/Erode/net.sf.cimg.CImgDilate.png;CImg/Erode/net.sf.cimg.CImgDilate.svg;CImg/Erode/net.sf.cimg.CImgErode.png;CImg/Erode/net.sf.cimg.CImgErode.svg;CImg/ErodeSmooth/net.sf.cimg.CImgErodeSmooth.png;CImg/ErodeSmooth/net.sf.cimg.CImgErodeSmooth.svg;CImg/Expression/net.sf.cimg.CImgExpression.png;CImg/Expression/net.sf.cimg.CImgExpression.svg;CImg/Guided/net.sf.cimg.CImgGuided.png;CImg/Guided/net.sf.cimg.CImgGuided.svg;CImg/HistEQ/net.sf.cimg.CImgHistEQ.png;CImg/HistEQ/net.sf.cimg.CImgHistEQ.svg;CImg/Noise/net.sf.cimg.CImgNoise.png;CImg/Noise/net.sf.cimg.CImgNoise.svg;CImg/Plasma/net.sf.cimg.CImgPlasma.png;CImg/Plasma/net.sf.cimg.CImgPlasma.svg;CImg/RollingGuidance/net.sf.cimg.CImgRollingGuidance.png;CImg/RollingGuidance/net.sf.cimg.CImgRollingGuidance.svg;CImg/SharpenInvDiff/net.sf.cimg.CImgSharpenInvDiff.png;CImg/SharpenInvDiff/net.sf.cimg.CImgSharpenInvDiff.svg;CImg/SharpenShock/net.sf.cimg.CImgSharpenShock.png;CImg/Smooth/net.sf.cimg.CImgSmooth.png;CImg/Smooth/net.sf.cimg.CImgSmooth.svg" 
    ""
    "${CMAKE_SOURCE_DIR}/SupportExt/ofxsLut.cpp;${CMAKE_SOURCE_DIR}/CImg/CImgFilter.cpp;${CMAKE_SOURCE_DIR}/CImg/CImgOperator.cpp")

# Misc.ofx (bundle)
create_ofx_plugin(Misc 
    "TimeBuffer;Add;AdjustRoD;Anaglyph;AppendClip;CheckerBoard;ChromaKeyer;Clamp;ClipTest;ColorBars;ColorCorrect;ColorLookup;ColorMatrix;ColorSuppress;ColorTransform;ColorWheel;Constant;CopyRectangle;CornerPin;Crop;Deinterlace;DenoiseSharpen;Despill;Difference;Dissolve;FrameBlend;FrameHold;FrameRange;Gamma;GodRays;Grade;HSVTool;HueCorrect;Distortion;ImageStatistics;Invert;JoinViews;Keyer;KeyMix;Log2Lin;MatteMonitor;Merge;Mirror;MixViews;Multiply;NoOp;OneView;PIK;PLogLin;Position;Premult;Quantize;Radial;Ramp;Rand;ReConverge;Rectangle;Reformat;Retime;Roto;Saturation;Shuffle;SideBySide;SlitScan;Switch;TimeBlur;TimeDissolve;TimeOffset;TrackerPM;Transform;VectorToColor" 
    "" 
    "Add/net.sf.openfx.AddPlugin.png;AdjustRoD/net.sf.openfx.AdjustRoDPlugin.png;AdjustRoD/net.sf.openfx.AdjustRoDPlugin.svg;Anaglyph/net.sf.openfx.anaglyphPlugin.png;Anaglyph/net.sf.openfx.anaglyphPlugin.svg;CheckerBoard/net.sf.openfx.CheckerBoardPlugin.png;CheckerBoard/net.sf.openfx.CheckerBoardPlugin.svg;ChromaKeyer/net.sf.openfx.ChromaKeyerPlugin.png;ChromaKeyer/net.sf.openfx.ChromaKeyerPlugin.svg;Clamp/net.sf.openfx.Clamp.png;Clamp/net.sf.openfx.Clamp.svg;ClipTest/net.sf.openfx.ClipTestPlugin.png;ClipTest/net.sf.openfx.ClipTestPlugin.svg;ColorBars/net.sf.openfx.ColorBars.png;ColorBars/net.sf.openfx.ColorBars.svg;ColorCorrect/net.sf.openfx.ColorCorrectPlugin.png;ColorCorrect/net.sf.openfx.ColorCorrectPlugin.svg;ColorLookup/net.sf.openfx.ColorLookupPlugin.png;ColorLookup/net.sf.openfx.ColorLookupPlugin.svg;ColorMatrix/net.sf.openfx.ColorMatrixPlugin.png;ColorMatrix/net.sf.openfx.ColorMatrixPlugin.svg;ColorWheel/net.sf.openfx.ColorWheel.png;ColorWheel/net.sf.openfx.ColorWheel.svg;ColorTransform/net.sf.openfx.HSVToRGB.png;ColorTransform/net.sf.openfx.HSVToRGB.svg;ColorTransform/net.sf.openfx.LabToRGB709.png;ColorTransform/net.sf.openfx.RGBToHSV.png;ColorTransform/net.sf.openfx.RGBToHSV.svg;ColorTransform/net.sf.openfx.RGB709ToLab.png;ColorTransform/net.sf.openfx.RGB709ToXYZ.png;ColorTransform/net.sf.openfx.XYZToRGB709.png;Constant/net.sf.openfx.ConstantPlugin.png;Constant/net.sf.openfx.ConstantPlugin.svg;CornerPin/net.sf.openfx.CornerPinMaskedPlugin.png;CornerPin/net.sf.openfx.CornerPinMaskedPlugin.svg;CornerPin/net.sf.openfx.CornerPinPlugin.png;CornerPin/net.sf.openfx.CornerPinPlugin.svg;Crop/net.sf.openfx.CropPlugin.png;Crop/net.sf.openfx.CropPlugin.svg;Deinterlace/net.sf.openfx.Deinterlace.png;Deinterlace/net.sf.openfx.Deinterlace.svg;Despill/net.sf.openfx.Despill.png;Despill/net.sf.openfx.Despill.svg;Difference/net.sf.openfx.DifferencePlugin.png;Difference/net.sf.openfx.DifferencePlugin.svg;Dissolve/net.sf.openfx.DissolvePlugin.png;Dissolve/net.sf.openfx.DissolvePlugin.svg;Distortion/net.sf.openfx.IDistort.png;Distortion/net.sf.openfx.IDistort.svg;Distortion/net.sf.openfx.STMap.png;Distortion/net.sf.openfx.STMap.svg;Distortion/net.sf.openfx.LensDistortion.png;Distortion/net.sf.openfx.LensDistortion.svg;FrameBlend/net.sf.openfx.FrameBlend.png;FrameBlend/net.sf.openfx.FrameBlend.svg;FrameHold/net.sf.openfx.FrameHold.png;FrameHold/net.sf.openfx.FrameHold.svg;FrameRange/net.sf.openfx.FrameRange.png;FrameRange/net.sf.openfx.FrameRange.svg;Gamma/net.sf.openfx.GammaPlugin.png;GodRays/net.sf.openfx.GodRays.png;GodRays/net.sf.openfx.GodRays.svg;Grade/net.sf.openfx.GradePlugin.png;Grade/net.sf.openfx.GradePlugin.svg;HSVTool/net.sf.openfx.HSVToolPlugin.png;HSVTool/net.sf.openfx.HSVToolPlugin.svg;ImageStatistics/net.sf.openfx.ImageStatistics.png;ImageStatistics/net.sf.openfx.ImageStatistics.svg;Invert/net.sf.openfx.Invert.png;Invert/net.sf.openfx.Invert.svg;JoinViews/net.sf.openfx.joinViewsPlugin.png;JoinViews/net.sf.openfx.joinViewsPlugin.svg;Keyer/net.sf.openfx.KeyerPlugin.png;Keyer/net.sf.openfx.KeyerPlugin.svg;Merge/net.sf.openfx.MergePlugin.png;Merge/net.sf.openfx.MergePlugin.svg;Merge/net.sf.openfx.MergeDifference.png;Merge/net.sf.openfx.MergeIn.png;Merge/net.sf.openfx.MergeMax.png;Merge/net.sf.openfx.MergeMin.png;Merge/net.sf.openfx.MergeMultiply.png;Merge/net.sf.openfx.MergeOut.png;Merge/net.sf.openfx.MergePlus.png;Merge/net.sf.openfx.MergeScreen.png;Mirror/net.sf.openfx.Mirror.png;Mirror/net.sf.openfx.Mirror.svg;MixViews/net.sf.openfx.mixViewsPlugin.png;MixViews/net.sf.openfx.mixViewsPlugin.svg;Multiply/net.sf.openfx.MultiplyPlugin.png;NoOp/net.sf.openfx.NoOpPlugin.png;NoOp/net.sf.openfx.NoOpPlugin.svg;Rand/net.sf.openfx.Noise.png;Rand/net.sf.openfx.Noise.svg;OneView/net.sf.openfx.oneViewPlugin.png;OneView/net.sf.openfx.oneViewPlugin.svg;Position/net.sf.openfx.Position.png;Position/net.sf.openfx.Position.svg;Premult/net.sf.openfx.Premult.png;Premult/net.sf.openfx.Premult.svg;Premult/net.sf.openfx.Unpremult.png;Premult/net.sf.openfx.Unpremult.svg;Radial/net.sf.openfx.Radial.png;Radial/net.sf.openfx.Radial.svg;Ramp/net.sf.openfx.Ramp.png;Ramp/net.sf.openfx.Ramp.svg;Rectangle/net.sf.openfx.Rectangle.png;Rectangle/net.sf.openfx.Rectangle.svg;Reformat/net.sf.openfx.Reformat.png;Reformat/net.sf.openfx.Reformat.svg;Retime/net.sf.openfx.Retime.png;Retime/net.sf.openfx.Retime.svg;Roto/net.sf.openfx.RotoPlugin.png;Roto/net.sf.openfx.RotoPlugin.svg;Saturation/net.sf.openfx.SaturationPlugin.png;Saturation/net.sf.openfx.SaturationPlugin.svg;Shuffle/net.sf.openfx.ShufflePlugin.png;Shuffle/net.sf.openfx.ShufflePlugin.svg;SideBySide/net.sf.openfx.sideBySidePlugin.png;SideBySide/net.sf.openfx.sideBySidePlugin.svg;SlitScan/net.sf.openfx.SlitScan.png;SlitScan/net.sf.openfx.SlitScan.svg;Switch/net.sf.openfx.switchPlugin.png;Switch/net.sf.openfx.switchPlugin.svg;TimeOffset/net.sf.openfx.timeOffset.png;TimeOffset/net.sf.openfx.timeOffset.svg;TrackerPM/net.sf.openfx.TrackerPM.png;TrackerPM/net.sf.openfx.TrackerPM.svg;Transform/net.sf.openfx.DirBlur.png;Transform/net.sf.openfx.DirBlur.svg;Transform/net.sf.openfx.TransformMaskedPlugin.png;Transform/net.sf.openfx.TransformMaskedPlugin.svg;Transform/net.sf.openfx.TransformPlugin.png;Transform/net.sf.openfx.TransformPlugin.svg;VectorToColor/net.sf.openfx.VectorToColorPlugin.png;VectorToColor/net.sf.openfx.VectorToColorPlugin.svg" 
    ""
    "${CMAKE_SOURCE_DIR}/SupportExt/ofxsGenerator.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsLut.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsMultiPlane.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsOGLTextRenderer.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsOGLFontData.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsRamp.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsShutter.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsTracking.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsTransform3x3.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsTransformInteract.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsRectangleInteract.cpp;${CMAKE_SOURCE_DIR}/Misc/randomGenerator.cpp")

