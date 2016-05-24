Host-specific OpenFX bugs and caveats:

* DaVinci Resolve Lite

OFX API version 1.3
hostName=DaVinciResolveLite
hostLabel=DaVinci Resolve Lite
hostVersion=12.5.0 (12.5)
hostIsBackground=0
supportsOverlays=1
supportsMultiResolution=0
supportsTiles=0
temporalClipAccess=1
supportedComponents=OfxImageComponentRGBA,OfxImageComponentAlpha
supportedContexts=OfxImageEffectContextFilter,OfxImageEffectContextGeneral,OfxImageEffectContextTransition,OfxImageEffectContextGenerator
supportedPixelDepths=OfxBitDepthFloat,OfxBitDepthShort,OfxBitDepthByte
supportsMultipleClipDepths=0
supportsMultipleClipPARs=0
supportsSetableFrameRate=0
supportsSetableFielding=0
supportsStringAnimation=0
supportsCustomInteract=0
supportsChoiceAnimation=0
supportsBooleanAnimation=0
supportsCustomAnimation=0
supportsParametricAnimation=0
canTransform=0
maxParameters=-1
pageRowCount=0
pageColumnCount=0
isNatron=0
supportsDynamicChoices=0
supportsCascadingChoices=0
supportsChannelSelector=0
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMessageSuite,OfxMessageSuiteV2,OfxProgressSuite,OfxTimeLineSuite

- version 11 of Resove Lite (from Mac App Store) does not support symbolic links in /Library/OFX/Plugins
- in Generators, even if the source clip is defined, it can not be fetched by the plug-in
- all defined clips will appear connected but give black and transparent (NULL) images. This is a problem for Mask clips, so a "Mask" boolean param must be added 
- kOfxImagePropField property is always kOfxImageFieldNone on OFX images
- OfxParameterSuiteV1::paramCopy doesn nothing, keys and values have to be copied explicitely (see CornerPin)
- The range AND display range has to be defined for all Double params (kOfxParamTypeDouble, kOfxParamTypeDouble2D, kOfxParamTypeDouble3D), or a default range of (-1,1) is used, and values cannot lie outsideof this range !
- The range AND display range has to be defined for Int params (kOfxParamTypeInteger), or a default range of (0,0) is used, and values cannot lie outsideof this range !
- kOfxPropPluginDescription property is absent from the plugin descriptor (although it was introduced in API version 1.2)
- kOfxParamPropDefaultCoordinateSystem (set by setDefaultCoordinateSystem() in Support) is not present on double parameters (although API version 1.3 is claimed), the only solution is to use a secret boolean and denormalize at instance creation, 
- kOfxParamTypeInteger2D kOfxParamTypeInteger3D are not supported (crash when opening the parameters page), at least in Generators
- kOfxImageEffectInstancePropSequentialRender property is missing on the host and the Image Effect descriptor (but exists on the image effect instance)
- kOfxImageEffectPropPluginHandle property is missing on the image effect instance
- kOfxPropType property on the image effect instance is 'OfxTypeImageEffect' instead of 'OfxTypeImageEffectInstance'
- kOfxPropHostOSHandle property is missing on the host
- the working directory when plugin code is executed, (where the ofxTextLog.txt file is written) is "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/Resources/"
- boolean params animate by default
- the OFX plugins cache is in "/Library/Application Support/Blackmagic Design/DaVinci Resolve/OFXPluginCache.xml"
- plugin descriptors may have the extra properties OfxImageEffectPropCudaRenderSupported OfxImageEffectPropOpenCLRenderSupported OfxImageEffectPropPlanarIOSupported (see below) OfxImageEffectPropSupportedComponents (redundant with clip descriptors?)
- there is an extra parameter type OfxParamTypeStrChoice
- other constants:
OfxImageEffectPropCudaEnabled (render arg?)
OfxImageEffectPropOpenCLEnabled (render arg?)
OfxImageEffectPropPlanarIOEnabled (render arg? images are represented by planes RRRGGGBBB instead of interleaved RRGBRGBRGB)
- extra clip property kOfxImageClipPropThumbnail (with a k)

* Nuke

OFX API version 1.2
type=Host
hostName=uk.co.thefoundry.nuke
hostLabel=nuke
hostVersion=10.0.1 (10.0)
hostIsBackground=0
supportsOverlays=1
supportsMultiResolution=1
supportsTiles=1
temporalClipAccess=1
supportedComponents=OfxImageComponentRGBA,OfxImageComponentAlpha,uk.co.thefoundry.OfxImageComponentMotionVectors,uk.co.thefoundry.OfxImageComponentStereoDisparity
supportedContexts=OfxImageEffectContextFilter,OfxImageEffectContextGeneral
supportedPixelDepths=OfxBitDepthFloat
supportsMultipleClipDepths=0
supportsMultipleClipPARs=0
supportsSetableFrameRate=0
supportsSetableFielding=0
supportsStringAnimation=0
supportsCustomInteract=1
supportsChoiceAnimation=1
supportsBooleanAnimation=1
supportsCustomAnimation=0
supportsParametricAnimation=0
canTransform=1
maxParameters=-1
pageRowCount=0
pageColumnCount=0
isNatron=0
supportsDynamicChoices=0
supportsCascadingChoices=0
supportsChannelSelector=0
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMessageSuite,OfxMessageSuiteV2,OfxProgressSuite,OfxTimeLineSuite,OfxParametricParameterSuite,NukeOfxCameraSuite,uk.co.thefoundry.FnOfxImageEffectPlaneSuiteV1,uk.co.thefoundry.FnOfxImageEffectPlaneSuiteV2

- ChoiceParam items can only be set during description and cannot be changed afterwards
- Params that are described as secret can never be "revealed", they are doomed to remain secret (fix: set them as secret at the end of effect instance creation)
- The Modelview matrix is not identity in interacts. Moreover, it is affected by successive transforms, so that the interact itself is affected by the transform.
- kOfxImageEffectInstancePropSequentialRender property is missing on the Image Effect descriptor (but exists on the host and on the effect instance)
- kOfxImageEffectPropPluginHandle property is missing on the image effect instance
- kOfxParamPropDefaultCoordinateSystem defaults to kOfxParamCoordinatesNormalised for XY and XYAbsolute!
- the OFX plugin cache is in /var/tmp/nuke-u501/ofxplugincache/ofxplugincache-501-*.xml



* Natron

OFX API version 1.3
hostName=fr.inria.Natron
hostLabel=Natron
hostVersion=2.0.0 (2.0.0)
hostIsBackground=0
supportsOverlays=1
supportsMultiResolution=1
supportsTiles=1
temporalClipAccess=1
supportedComponents=OfxImageComponentRGBA,OfxImageComponentAlpha,OfxImageComponentRGB,uk.co.thefoundry.OfxImageComponentMotionVectors,uk.co.thefoundry.OfxImageComponentStereoDisparity
supportedContexts=OfxImageEffectContextGenerator,OfxImageEffectContextFilter,OfxImageEffectContextGeneral,OfxImageEffectContextTransition
supportedPixelDepths=OfxBitDepthFloat,OfxBitDepthShort,OfxBitDepthByte
supportsMultipleClipDepths=1
supportsMultipleClipPARs=0
supportsSetableFrameRate=0
supportsSetableFielding=0
supportsStringAnimation=1
supportsCustomInteract=1
supportsChoiceAnimation=1
supportsBooleanAnimation=1
supportsCustomAnimation=1
supportsParametricAnimation=0
canTransform=1
maxParameters=-1
pageRowCount=0
pageColumnCount=0
isNatron=1
supportsDynamicChoices=1
supportsCascadingChoices=1
supportsChannelSelector=1
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMessageSuite,OfxMessageSuiteV2,OfxProgressSuite,OfxTimeLineSuite,OfxParametricParameterSuite,uk.co.thefoundry.FnOfxImageEffectPlaneSuiteV1,uk.co.thefoundry.FnOfxImageEffectPlaneSuiteV2,OfxVegasStereoscopicImageEffectSuite

- may give a fake hostName for plugins that don't officially support Natron, but sets an extra host property kNatronOfxHostIsNatron
- the isidentity action may point to a frame on the output clip, which is useful for generators and readers

* Sony Catalyst Edit

OFX API version 1.3
type=OfxTypeImageEffectHost
hostName=com.sony.Catalyst.Edit
hostLabel=Catalyst Edit 2015.1
hostVersion=2015.1.1 (2015.1)
hostIsBackground=0
supportsOverlays=0
supportsMultiResolution=0
supportsTiles=0
temporalClipAccess=1
supportedComponents=OfxImageComponentNone,OfxImageComponentRGBA,OfxImageComponentAlpha
supportedContexts=OfxImageEffectContextGeneral,OfxImageEffectContextGenerator,OfxImageEffectContextFilter,OfxImageEffectContextTransition,OfxImageEffectContextRetimer,OfxImageEffectContextPaint
supportedPixelDepths=OfxBitDepthByte,OfxBitDepthShort,OfxBitDepthHalf,OfxBitDepthFloat
supportsMultipleClipDepths=0
supportsMultipleClipPARs=0
supportsSetableFrameRate=0
supportsSetableFielding=0
supportsStringAnimation=1
supportsCustomInteract=0
supportsChoiceAnimation=1
supportsBooleanAnimation=1
supportsCustomAnimation=0
supportsParametricAnimation=0
canTransform=0
maxParameters=1000
pageRowCount=0
pageColumnCount=0
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMessageSuite,OfxProgressSuite,OfxImageEffectOpenGLRenderSuite,OfxOpenCLProgramSuite,

- OfxImageEffectSuiteV1::clipGetRegionOfDefinition() returns the RoD in pixels, and depends on the current renderScale !!! Sony wrongly implemented http://openeffects.org/standard_changes/properties-that-are-doubles-canonical-but-should-really-be-ints-in-pixels-space in OFX 1.3
- supports the undocumented suite OfxOpenCLProgramSuite, and the property OfxImageEffectPropOpenCLSupported on plugin descriptors
- the OFX Log is "/Applications/ofxTestLog.txt"
- the OFX plugin cache is in "~/Library/Application Support/Sony/Catalyst Edit/2015.1/plugincache.xml
