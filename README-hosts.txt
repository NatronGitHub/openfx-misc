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
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMultiThreadSuite,OfxMessageSuite,OfxMessageSuiteV2,OfxProgressSuite,OfxTimeLineSuite,OfxImageEffectOpenGLRenderSuite

Caveats/Bugs:
- Resolve 14: claims it has OpenFX message suite V2, but setPersistentMessage is NULL and clearPersistentMessage is garbage.
- Resolve 14: OpenGL render is never called, but an OpenGL context is attached when render action is called (thus the plugin may perform offscreen rendering)
- version 11 of Resolve Lite (from Mac App Store) does not support symbolic links in /Library/OFX/Plugins
- in Generators, even if the source clip is defined, it can not be fetched by the plug-in (the source clip should always be fetchable, it is mandatory in OpenFX)
- all defined clips will appear connected (property kOfxImageClipPropConnected = 1) but give black and transparent (NULL) images. This is a problem for Mask clips, so a "Mask" boolean param must be added specifically for Resolve (see kParamMaskApply in openfx-misc)
- kOfxImagePropField property is always kOfxImageFieldNone on OFX images, regardless of the clip properties
- OfxParameterSuiteV1::paramCopy does nothing, keys and values have to be copied explicitely (see CornerPin)
- even though OfxImageEffectOpenGLRenderSuite exists, the render action is never called with OpenGL enabled (is Resolve supposed to support OpenGL rendering?)
- The range AND display range has to be defined for all Double params (kOfxParamTypeDouble, kOfxParamTypeDouble2D, kOfxParamTypeDouble3D), or a default range of (-1,1) is used, and values cannot lie outsideof this range !
- The range AND display range has to be defined for Int params (kOfxParamTypeInteger), or a default range of (0,0) is used, and values cannot lie outside of this range !
- kOfxPropPluginDescription property is absent from the plugin descriptor (although it was introduced in API version 1.2)
- kOfxParamPropDefaultCoordinateSystem (set by setDefaultCoordinateSystem(eCoordinatesNormalised) in Support) is not present on double parameters (although API version 1.3 is claimed), the only solution is to use a secret boolean and denormalize at instance creation (see kParamDefaultsNormalised in openfx-misc)
- kOfxParamTypeInteger2D kOfxParamTypeInteger3D are not supported (crash when opening the parameters page), at least in Generators
- kOfxImageEffectInstancePropSequentialRender property is missing on the host and the Image Effect descriptor (but exists on the image effect instance)
- kOfxImageEffectPropPluginHandle property is missing on the image effect instance
- kOfxPropType property on the image effect instance is 'OfxTypeImageEffect' instead of 'OfxTypeImageEffectInstance'
- kOfxPropHostOSHandle property is missing on the host (it has to be present, even if it is NULL)
- boolean params animate by default (they should not, according to the OFX specs)
- kOfxParamPropEvaluateOnChange property on parameters is not used: any parameter change causes a new render!
- kOfxImageEffectActionRender is called to generate thumbnails, but the renderscale argument is always (1,1) although it should be much smaller

Extensions:
- plugin descriptors may have the extra properties OfxImageEffectPropCudaRenderSupported OfxImageEffectPropOpenCLRenderSupported OfxImageEffectPropPlanarIOSupported (see below) OfxImageEffectPropSupportedComponents (redundant with clip descriptors?)
- there is an extra parameter type OfxParamTypeStrChoice, which is like a string param, but returns a string for each choice.
- other constants:
OfxImageEffectPropCudaEnabled (render arg?)
OfxImageEffectPropOpenCLEnabled (render arg?)
OfxImageEffectPropOpenCLCommandQueue (render arg?)
OfxImageEffectPropPlanarIOEnabled (render arg? images are represented by planes RRRGGGBBB instead of interleaved RRGBRGBRGB)
- extra clip property kOfxImageClipPropThumbnail (with a k)

Misc:
- the working directory when plugin code is executed, (where the ofxTextLog.txt file is written) is "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/Resources/"
- OFX Log is "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/Resources/ofxTestLog.txt"
- the OFX plugins cache is in "/Library/Application Support/Blackmagic Design/DaVinci Resolve/OFXPluginCache.xml"

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
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMultiThreadSuite,OfxMessageSuite,OfxMessageSuiteV2,OfxProgressSuite,OfxTimeLineSuite,OfxParametricParameterSuite,NukeOfxCameraSuite,uk.co.thefoundry.FnOfxImageEffectPlaneSuiteV1,uk.co.thefoundry.FnOfxImageEffectPlaneSuiteV2

- ChoiceParam items can only be set during description and cannot be changed afterwards
- Parameter labels are not saved, even if they are changed after instance creation: when the project is loaded, the label set in the descriptor is used
- Parameter hints (kOfxParamPropHint) can be set on parameter instances, but are not taken into account
- StringParams of type kOfxParamStringIsLabel do not have their value saved, and cannot be changed after Instance creation. The label displayed is thus always the default value of the string
- Params that are described as secret can never be "revealed", they are doomed to remain secret (fix: set them as secret at the end of effect instance creation)
- The Modelview matrix is not identity in interacts. Moreover, it is affected by successive transforms, so that the interact itself is affected by the transform.
- kOfxImageEffectInstancePropSequentialRender property is missing on the Image Effect descriptor (but exists on the host and on the effect instance)
- kOfxImageEffectPropPluginHandle property is missing on the image effect instance
- kOfxParamPropDefaultCoordinateSystem defaults to kOfxParamCoordinatesNormalised for XY and XYAbsolute!
- the OFX plugin cache is in /var/tmp/nuke-u501/ofxplugincache/ofxplugincache-501-*.xml



* Natron

OFX API version 1.4
hostName=fr.inria.Natron
hostLabel=Natron
hostVersion=2.1.0 (2.1.0)
hostIsBackground=0
supportsOverlays=1
supportsMultiResolution=1
supportsTiles=1
temporalClipAccess=1
supportedComponents=OfxImageComponentRGBA,OfxImageComponentAlpha,OfxImageComponentRGB,uk.co.thefoundry.OfxImageComponentMotionVectors,uk.co.thefoundry.OfxImageComponentStereoDisparity
supportedContexts=OfxImageEffectContextGenerator,OfxImageEffectContextFilter,OfxImageEffectContextGeneral,OfxImageEffectContextTransition
supportedPixelDepths=OfxBitDepthFloat,OfxBitDepthShort,OfxBitDepthByte
supportsMultipleClipDepths=1
supportsMultipleClipPARs=1
supportsSetableFrameRate=1
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
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMultiThreadSuite,OfxMessageSuite,OfxMessageSuiteV2,OfxProgressSuite,OfxTimeLineSuite,OfxParametricParameterSuite,OfxImageEffectOpenGLRenderSuite,uk.co.thefoundry.FnOfxImageEffectPlaneSuiteV1,uk.co.thefoundry.FnOfxImageEffectPlaneSuiteV2,OfxVegasProgressSuite,OfxVegasStereoscopicImageEffectSuite

- may give a fake hostName for plugins that don't officially support Natron, but sets an extra host property kNatronOfxHostIsNatron
- the isidentity action may point to a frame on the output clip, which is useful for generators and readers
- the OFX plugin cache is in  ~/Library/Caches/INRIA/Natron/OFXLoadCache/OFXCache_*.xml

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
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMultiThreadSuite,OfxMessageSuite,OfxProgressSuite,OfxImageEffectOpenGLRenderSuite,OfxOpenCLProgramSuite,

- Non-conformant behavior: OfxImageEffectSuiteV1::clipGetRegionOfDefinition() returns the RoD in pixels, and depends on the current renderScale!!! Sony wrongly implemented http://openeffects.org/standard_changes/properties-that-are-doubles-canonical-but-should-really-be-ints-in-pixels-space in OFX 1.3
- OfxParamTypeDouble2D parameters may have optional properties OfxCatalystParamPropLinkedParameterLabel and OfxCatalystParamPropLinkedParameterType (which can only be "rectangle") it links one parameter to the next described double2D parameter to form a rectangle between both corners
- OfxParamTypeDouble2D parameters have a maximum setable range of -9.99..9.99 in the Gui. Consequently, deprecated
- Suites:
  - supports the undocumented suite OfxOpenCLProgramSuite, and the property OfxImageEffectPropOpenCLSupported on plugin descriptors
  - no OfxMessageSuiteV2
  - OfxMultiThreadSuite misses the mutex-related functions
  - OfxParameterSuiteV1::paramCopy returns kOfxStatErrUnknown and does nothing
  - the OpenGL render context has no depth buffer, the projection is identity, the image transform is all in the modelview, and a glScissor is set to the renderWindow
  - the OpenGL actions kOfxActionOpenGLContextAttached and kOfxActionOpenGLContextDetached are never called
- undocumented action: OfxCatalystImageEffectActionGetRegionOfDefinition (note that the kOfxImageEffectActionGetRegionOfDefinition action is never called). Only implemented by the Sony Crop plugin? inArgs seem to be kOfxPropTime  and kOfxImageEffectPropRenderScale  and outArgs seem to be OfxImageEffectPropRegionOfDefinition , which are the same as the OFX action.
- undocumented plugin descriptor property: OfxCatalystImageEffectHidden
- undocumented param property OfxCatalystParamPropNeedsProgress

- the OFX Log is "/Applications/ofxTestLog.txt"
- the OFX plugin cache is in "~/Library/Application Support/Sony/Catalyst Edit/2015.1/plugincache.xml
