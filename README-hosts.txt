Host-specific OpenFX bugs and caveats:

Each entry should give its source: a URL, an issue or PR number, a commit, a code comment, or
"observed in <host> <version>, <date>". Entries marked (unconfirmed) come from third-party reports
(other plug-ins' issues, notes or code) and were not reproduced here; some of these sources are
partly AI-written.

* Several hosts

- (unconfirmed) animated Choice parameters: in Resolve and Silhouette, paramGetValue returns the value of the first key; Nuke, Baselight, Scratch and Fusion interpolate choices linearly (in Fusion, only its OFX host does: native Fusion tools step choices). Source: AcademySoftwareFoundation/openfx#116.
- (unconfirmed) clips at project load: Baselight's clips are not connected yet at createInstance; Scratch connects them afterwards and sends kOfxActionInstanceChanged with kOfxChangeUserEdited, and Silhouette did the same from 2025-06. A plug-in that sets parameters when a clip gets connected thus overwrites the values restored from the project (FrameHold only does it on hosts known to skip that, see hostSkipsChangedClipOnLoad()). Sources: AcademySoftwareFoundation/openfx#184 and #294.
- (unconfirmed) Resolve, Fusion and Flame: the interactive and sequential render flags (kOfxImageEffectPropInteractiveRenderStatus, kOfxImageEffectPropSequentialRenderStatus) of the render action can't be relied on, e.g. to tell a final render from a viewer render. Source: creativevideotips/cvt-ltx-alpha-gen (https://github.com/creativevideotips/cvt-ltx-alpha-gen), src/upstream-notes/ofx.md (Flame: every render reports interactive=0, sequential=1) and src/ofx/src/plugin.cpp (Fusion: interactive=0 for viewer renders too).


* Blackmagic DaVinci Resolve Lite

Version 12.5:

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

Version 16:

OFX API version 1.4
hostName=DaVinciResolveLite
hostLabel=DaVinci Resolve Lite
hostVersion=16.0.0 (16.0)
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
supportsChoiceAnimation=1
supportsBooleanAnimation=1
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
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMultiThreadSuite,OfxMessageSuite,OfxProgressSuite,OfxTimeLineSuite

Version 19 (19.1.4):

No full host description was recorded. What is known, from a user's Resolve 19.1.4 on macOS arm64 and the logs they posted (https://github.com/NatronGitHub/openfx-misc/issues/69, 2025):
- the host description does not advertise OpenGL render support (kOfxImageEffectPropOpenGLRenderSupported), and render is always called with OpenGL disabled.
- on the Edit and Color pages, an OpenGL context is current when the render action is called (OpenGL 2.1 with GLSL 1.20 on Apple Silicon).
- on the Fusion page, render is called from threads that have no current OpenGL context, and any OpenGL call crashes Resolve.

Caveats/Bugs:
- Resolve 14: claims it has OpenFX message suite V2, but setPersistentMessage is NULL and clearPersistentMessage is garbage. Source: NatronGitHub/openfx commit 1ddffffc (2018, "clearPersistentMessage contains garbage on Resolve"), see https://github.com/NatronGitHub/openfx-misc/issues/46.
- Resolve 14: OpenGL render is never called, but an OpenGL context is attached when render action is called (thus the plugin may perform offscreen rendering)
- version 11 of Resolve Lite (from Mac App Store) does not support symbolic links in /Library/OFX/Plugins
- in Generators, even if the source clip is defined, it can not be fetched by the plug-in. The generator context has no mandated input clip ("Any input clips that are specified must be optional.", OpenFX ofxImageEffectContexts.rst, "The Generator Context"), and since Resolve reports every clip as connected but returns NULL images (see below), plug-ins must handle a NULL image anyway. Source: README-hosts.txt history (41325c46, 2015, Resolve Lite).
- all defined clips will appear connected (property kOfxImageClipPropConnected = 1) but give black and transparent (NULL) images. This is a problem for Mask clips, so a "Mask" boolean param must be added specifically for Resolve (see kParamMaskApply in openfx-misc, ofxsMaskIsAlwaysConnected() in SupportExt's ofxsMaskMix.h, and NatronGitHub/openfx-supportext#183)
- kOfxImagePropField property is always kOfxImageFieldNone on OFX images, regardless of the clip properties
- OfxParameterSuiteV1::paramCopy does nothing, keys and values have to be copied explicitly (see the comments in CornerPin.cpp)
- even though OfxImageEffectOpenGLRenderSuite exists, the render action is never called with OpenGL enabled (is Resolve supposed to support OpenGL rendering?)
- The range AND display range has to be defined for all Double params (kOfxParamTypeDouble, kOfxParamTypeDouble2D, kOfxParamTypeDouble3D), or a default range of (-1,1) is used, and values cannot lie outsideof this range !
- The range AND display range has to be defined for Int params (kOfxParamTypeInteger), or a default range of (0,0) is used, and values cannot lie outside of this range !
- kOfxPropPluginDescription property is absent from the plugin descriptor (although it was introduced in API version 1.2)
- kOfxParamPropDefaultCoordinateSystem (set by setDefaultCoordinateSystem(eCoordinatesNormalised) in Support) is not present on double parameters (although API version 1.3 is claimed), the only solution is to denormalize at instance creation: new code uses SupportExt's ofxsNormalisedDefaults.h (ofxsSetDefaultCoordinateSystemNormalised() in describe, ofxsDenormaliseUntouchedDefault() in the constructor, e.g. Position, CopyRectangle), older code a secret boolean (see kParamDefaultsNormalised in openfx-misc, e.g. CornerPin, ofxsGenerator). Source: https://github.com/NatronGitHub/openfx-misc/issues/46 (the 1x1 DenoiseSharpen analysis box).
- kOfxParamTypeInteger2D kOfxParamTypeInteger3D are not supported (crash when opening the parameters page), at least in Generators
- kOfxImageEffectInstancePropSequentialRender property is missing on the host and the Image Effect descriptor (but exists on the image effect instance). See the "missing in Resolve" comments in openfx Support's ofxsImageEffect.cpp and TimeBuffer.cpp.
- kOfxImageEffectPropPluginHandle property is missing on the image effect instance
- kOfxPropType property on the image effect instance is 'OfxTypeImageEffect' instead of 'OfxTypeImageEffectInstance'
- kOfxPropHostOSHandle property is missing on the host (it has to be present, even if it is NULL)
- boolean params animate by default (they should not, according to the OFX specs)
- kOfxParamPropEvaluateOnChange property on parameters is not used: any parameter change causes a new render!
- kOfxImageEffectActionRender is called to generate thumbnails, but the renderscale argument is always (1,1) although it should be much smaller. Source: README-hosts.txt history (864c348d, 2017, Resolve 12.5). Newer versions differ: see the Resolve 21.0.4 thumbnail lead below.
- OfxMultiThreadSuite is a dummy implementation (CPU plug-ins don't run multithreaded): on Resolve, SupportExt's ofxsThreadSuiteCheck() (ofxsThreadSuite.cpp, comment "Resolve has a dummy MT Suite, use our own") replaces it with its own implementation, as it does on hosts that miss some of the suite's functions. Source: https://github.com/NatronGitHub/openfx-misc/issues/46.
- the render action is always the CPU render (see "Version 19" above), but an OpenGL context may be current: Shadertoy and TestOpenGL render with it if one is current (openGLContextIsCurrent(), see ShadertoyRenderGL.cpp), and else with OSMesa. On the Fusion page, there is no current context, and OpenGL calls crash Resolve. Without OSMesa, both plug-ins refuse to load on a host that does not advertise OpenGL render support, so their "no context" error path is only defensive. Source: https://github.com/NatronGitHub/openfx-misc/issues/69 (Resolve 19.1.4) and #127.
- in kOfxImageEffectActionGetFramesNeeded, the out args have no OfxImageClipPropFrameRange_<clip> property for the clips that are not connected: the Support library (framesNeededAction() in ofxsImageEffect.cpp, comment "The host may not have the property if the clip is not connected (Resolve)") skips them instead of failing. Source: NatronGitHub/openfx commit 3e2c0c87 (2016-02-03, "Support: do not fail in getFramesNeeded if output properties do not exist").
- images fetched with clipGetImage from kOfxActionInstanceChanged (e.g. on a button press) have the right bounds but garbage content, so DenoiseSharpen's Analysis does not work. Source: https://github.com/NatronGitHub/openfx-misc/issues/46 (Resolve 14).
- OpenGL overlays are drawn too small (as in Fusion): the plug-ins with overlays have a "HiDPI" checkbox. Source: https://github.com/NatronGitHub/openfx-misc/issues/46.

Leads for newer versions (unconfirmed, not reproduced here):
- (unconfirmed) Resolve 21.0.4 (macOS App Store), Cut page: the Effects side panel takes the effect label only from kOfxPropLongLabel, without falling back to kOfxPropLabel, so the label is empty if the plug-in sets only kOfxPropLabel. Source: ntsc-rs/ntsc-rs#875 (a PR, with screenshots). NatronGitHub/openfx#351 makes the Support library's setLabel() set the short and long labels too.
- (unconfirmed) Resolve 2026, Edit and Color pages: a failed render is silent, and the viewer (and a Deliver) shows the plain footage; the Fusion page and Fusion Studio show a red node. Source: creativevideotips/cvt-ltx-alpha-gen (https://github.com/creativevideotips/cvt-ltx-alpha-gen), src/ofx/src/plugin.cpp.
- (unconfirmed) Resolve and Fusion 2026: persistent messages are refused (setting and clearing), sendMessage(kOfxMessageQuestion) returns kOfxStatFailed, and a kOfxParamStringIsLabel parameter shows its name, not its value. Source: creativevideotips/cvt-ltx-alpha-gen (https://github.com/creativevideotips/cvt-ltx-alpha-gen), src/ofx/src/plugin.cpp and src/upstream-notes/ofx.md.
- (unconfirmed) Resolve 21.1: every clip frame range is the whole media file (or multicam), the effect instance frame range is 0..0, and on the Fusion page the OFX time is the comp frame plus an offset. Sources: gyroflow/gyroflow-plugins#39; https://github.com/dpovey/gigsteady/blob/HEAD/docs/OFX-EXPLORATION.md.
- (unconfirmed) Resolve 21.0.4: thumbnails are rendered at their real render scale, which may be non-uniform (184x92 images at renderScale 0.04x0.02 and 0.02x0.01), so the "(1,1)" entry above is outdated. Source: the capture logs of light-sail-vr/lsvr-vrndi-resolve-plugin, docs/captures/2026-08-28-stereo-timeline-edit-page.log and 2026-08-28-stereo-timeline-color-page.log (its docs/2026-08-28-render-call-probe-findings.md reports the 184x92 thumbnail renders, but not their scale).
- (unconfirmed) Resolve 2026: the plug-in process runs with umask 000, so the files a plug-in creates are world-writable unless it sets their mode. Source: creativevideotips/cvt-ltx-alpha-gen (https://github.com/creativevideotips/cvt-ltx-alpha-gen), src/ofx/src/plugin.cpp.
- (unconfirmed) a plug-in that returns kOfxStatReplyDefault for kOfxActionCreateInstance or kOfxActionDestroyInstance is loaded, but shows no controls and never renders, although the spec allows that reply. Support-based plug-ins are not affected: the Support library's mainEntry returns kOfxStatOK for both. Source: ntsc-rs, https://github.com/ntsc-rs/ntsc-rs/blob/8cfc51014092a6dc1da5eb0217bb9ae6516b124e/crates/openfx-plugin/src/lib.rs#L1596.
- (unconfirmed) the plug-ins' stderr is not shown, but their stdout is. Sources: ntsc-rs, https://github.com/ntsc-rs/ntsc-rs/blob/8cfc51014092a6dc1da5eb0217bb9ae6516b124e/crates/openfx-plugin/src/lib.rs#L1621 (panic messages are printed on stdout "so Resolve doesn't swallow" them); the DEBUG_STDOUT comment in DenoiseSharpen.cpp ("output debugging messages on stdout (for Resolve)").
- (unconfirmed) Edit and Cut pages: effects get the clip after it is scaled to the timeline resolution; on the Fusion page, they get it before. Source: https://github.com/ntsc-rs/ntsc-rs-website/blob/main/src/docs/openfx-plugin.md (lines 30-49).
- (unconfirmed) the effects menu groups plug-ins by vendor, not by kOfxImageEffectPluginPropGrouping. Source: the same ntsc-rs page ("In software that groups plugins by vendor instead of by category, such as DaVinci Resolve").

Extensions:
- plugin descriptors may have the extra properties OfxImageEffectPropCudaRenderSupported OfxImageEffectPropOpenCLRenderSupported OfxImageEffectPropPlanarIOSupported (see below) OfxImageEffectPropSupportedComponents (redundant with clip descriptors?)
- Resolve's extension OfxParamTypeStrChoice (like a string param, but returns a string for each choice), standardised in OFX 1.5 (ofxParam.h; proposed in AcademySoftwareFoundation/openfx#129).
- other constants:
OfxImageEffectPropCudaEnabled (render arg?)
OfxImageEffectPropOpenCLEnabled (render arg?)
OfxImageEffectPropOpenCLCommandQueue (render arg?)
OfxImageEffectPropPlanarIOEnabled (render arg? images are represented by planes RRRGGGBBB instead of interleaved RRGBRGBRGB)
- extra clip property kOfxImageClipPropThumbnail (with a k)

Misc:
- the working directory when plugin code is executed, (where the debug-build OFX log is written) is "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/Resources/"
- OFX Log is "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/Resources/ofxPluginLog.txt" (ofxTestLog.txt with openfx before 1.5.1)
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
- Nuke 8 to 11 (11.1 is the last version known to be broken): OfxParametricParameterSuite exists, but parametric parameters do not work: the plugin cannot be instantiated (see README.md). ColorCorrect, ColorLookup, HueCorrect (both versions), Retime and TimeDissolve treat hostName uk.co.thefoundry.nuke with a major version from 8 to 11 as a host without parametric parameters (comment "Nuke 8-11.1 are known to *not* support Parametric").
- Nuke 7: if Shadertoy has more than 7 extra parameters (uniforms), the parameter page goes blank when the Extra Parameters group is unfolded: Shadertoy defines only SHADERTOY_NBUNIFORMS_NUKE7 (7) of them on Nuke 7 (Shadertoy.h, and the comment in Shadertoy.cpp).
- returning an error from the describe action (e.g. throwHostMissingSuiteException() when a needed suite is missing) crashes Nuke: such checks in describe are commented out (see the "returning an error here crashes Nuke" comments, e.g. in ColorLookup, Shadertoy, Anaglyph), and the checks that are needed are done in describeInContext (e.g. ColorLookup, HueCorrect, Shadertoy). Returning an error from the load action gives a blank menu entry (see TestOpenGL.cpp).
- after setPersistentMessage(), render is not called again until the message is cleared: plugins that may set one call clearPersistentMessage() in changedParam (e.g. SupportExt's ofxsTransform3x3.cpp, Reformat, CImgExpression, CImgMatrix).
- kOfxActionInstanceChanged is not called when a linked animation changes, so the plugin can not cache values that depend on the parameter animation (see ofxsTransform3x3.cpp).
- the image unique identifier (kOfxImagePropUniqueIdentifier) is "ffffffffffffffff", so it can not be used to detect that an image changed (see ofxsTransform3x3.cpp).
- images cannot be fetched at fractional times: effects that blend sub-frames, like TimeBlur, do not work correctly (TimeBlur says so in its description on Nuke).
- string parameter values go through TCL substitution: the characters '$', '[' and ']' must be preceded by a backslash ('\$', '\[', '\]'). No openfx-misc plugin works around it; openfx-io's SeExpr and RunScript mention it in their parameter hints (kNukeWarnTcl, the source of this entry).
- the OFX plugin cache (XML), written by old versions of HostSupport's ofxhXml.h, is corrupted by control characters (U+0001..U+001F except tab, newline and return, and U+007F..U+009F, in the label and grouping), '<' and '&' in the plugin strings. openfx-gmic strips them on hosts other than Natron (validateXMLString() in Helpers/RFX_OFX_Utils.h, comment "workaround for bugs in the OpenFX plugin cache, e.g. in Nuke"). openfx-misc does not strip them, but in debug builds the Support library warns about them (validateXMLString() in openfx/Support/Library/ofxsImageEffect.cpp).
- (unconfirmed) Nuke 17: each setPersistentMessage() call appends a new line to the Error Console, even with the same message id. Source: CorridorKey-Runtime (https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime), src/plugins/ofx/ofx_shared.hpp.
- (unconfirmed) Nuke 14.0v1 crashed when a plug-in read kFnOfxImageEffectPropView; fixed in 14.0v2. Source: Nuke 14.0v2 release notes (ID 532002; no stable URL found).
- (unconfirmed) before Nuke 16.1, OFX images were limited to 8192x8192. Source: Nuke release notes 16.1v1 and 17.0v1 (no stable URL found).
- (unconfirmed) Nuke 16 supports OFX 1.5, with the DrawSuite. Source: Nuke release notes 16.0v1 (IDs 588795 and 587935; no stable URL found).
- (unconfirmed) the node name is made from the plug-in label, and characters that are not allowed in node names give a warning such as: Node name "NTSC-rs1" contains the illegal character(s) "-" (seen in Nuke 11.1 and 15.1). Source: ntsc-rs/ntsc-rs#312. In openfx-misc, "G'MIC Expression" (CImgExpression.cpp) has such a character.



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

- Non-conformant behavior: OfxImageEffectSuiteV1::clipGetRegionOfDefinition() returns the RoD in pixels, and depends on the current renderScale!!! Sony wrongly implemented http://openeffects.org/standard_changes/properties-that-are-doubles-canonical-but-should-really-be-ints-in-pixels-space in OFX 1.3 (see also the comments "In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels" that the helper below replaced)
  Catalyst Edit does not support multi-resolution, so all inputs have the same RoD anyway: the isIdentity test "the mask RoD does not intersect the render window" must be skipped on hosts without multi-resolution. SupportExt's ofxsMaskRoDExcludesRenderWindow() (ofxsRGBAProcessing.h) does this test, and skips it on those hosts.
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

- the OFX Log is "/Applications/ofxPluginLog.txt" (ofxTestLog.txt with openfx before 1.5.1)
- the OFX plugin cache is in "~/Library/Application Support/Sony/Catalyst Edit/2015.1/plugincache.xml

* Sony Vegas / VEGAS Pro (MAGIX)

Version 12:

OFX API version 1.1
hostName=com.sonycreativesoftware.vegas
hostLabel=Vegas Pro 12.0
hostVersion=12.726.0 ()
hostIsBackground=0
supportsOverlays=0
supportsMultiResolution=0
supportsTiles=0
temporalClipAccess=1
supportedComponents=OfxImageComponentNone,OfxImageComponentRGBA,OfxImageComponentAlpha
supportedContexts=OfxImageEffectContextGeneral,OfxImageEffectContextGenerator,OfxImageEffectContextFilter,OfxImageEffectContextTransition,OfxImageEffectContextRetimer,OfxImageEffectContextPaint
supportedPixelDepths=OfxBitDepthByte,OfxBitDepthFloat
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
isNatron=0
supportsDynamicChoices=0
supportsCascadingChoices=0
supportsChannelSelector=0
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMultiThreadSuite,OfxMessageSuite,OfxMessageSuiteV2,OfxProgressSuite,OfxTimeLineSuite,OfxImageEffectOpenGLRenderSuite,OfxVegasProgressSuite,OfxVegasStereoscopicImageEffectSuite,OfxVegasKeyframeSuite,OfxOpenCLProgramSuite,

Version 15:

OFX API version 1.1
hostName=com.sonycreativesoftware.vegas
hostLabel=VEGAS Pro 15.0
hostVersion=15.416.0 ()
hostIsBackground=0
supportsOverlays=0
supportsDrawSuite=0
supportsMultiResolution=0
supportsTiles=0
temporalClipAccess=1
supportedComponents=OfxImageComponentNone,OfxImageComponentRGBA,OfxImageComponentAlpha
supportedContexts=OfxImageEffectContextGeneral,OfxImageEffectContextGenerator,OfxImageEffectContextFilter,OfxImageEffectContextTransition,OfxImageEffectContextRetimer,OfxImageEffectContextPaint
supportedPixelDepths=OfxBitDepthByte,OfxBitDepthFloat
supportsMultipleClipDepths=0
supportsMultipleClipPARs=0
supportsSetableFrameRate=0
supportsSetableFielding=0
sequentialRender=0
numCPUs=1
supportsRenderQualityDraft=0
supportsStringAnimation=1
supportsCustomInteract=0
supportsChoiceAnimation=1
supportsStrChoice=0
supportsStrChoiceAnimation=0
supportsBooleanAnimation=1
supportsCustomAnimation=0
supportsParametricParameter=0
supportsParametricAnimation=0
canTransform=0
nativeOrigin=kOfxImageEffectHostPropNativeOriginBottomLeft
osHandle=0
supportsCPURender=1
supportsOpenGLRender=0
supportsOpenCLRender=0
supportsCudaRender=0
supportsCudaStream=0
supportsMetalRender=0
supportsMetalTexture=0
colourManagementStyle=OfxImageEffectColourManagementNone
colourManagementAvailableConfigs=
supportsCamera=0
isMultiPlanar=0
maxParameters=1000
maxPages=0
pageRowCount=0
pageColumnCount=0
supportsProgressSuite=1
supportsTimeLineSuite=1
supportsMessageSuiteV2=1
isNatron=0
supportsDynamicChoices=0
supportsCascadingChoices=0
supportsChannelSelector=0
canDistort=0
supportsInteractKeyModifiers=0
nativeOverlays=
supportedCursors=
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMultiThreadSuite,OfxMessageSuite,OfxMessageSuiteV2,OfxProgressSuite,OfxTimeLineSuite,OfxInteractSuite,OfxImageEffectOpenGLRenderSuite,OfxOpenCLProgramSuite,OfxVegasProgressSuite,OfxVegasStereoscopicImageEffectSuite,OfxVegasKeyframeSuite,OfxHWndInteractSuite

Version 19:

OFX API version 1.1
hostName=com.vegascreativesoftware.vegas
hostLabel=VEGAS Pro 19.0
hostVersion=19.651.0 (VEGAS Pro 19.0)
hostIsBackground=0
supportsOverlays=0
supportsDrawSuite=0
supportsMultiResolution=0
supportsTiles=0
temporalClipAccess=1
supportedComponents=OfxImageComponentNone,OfxImageComponentRGBA,OfxImageComponentAlpha
supportedContexts=OfxImageEffectContextGeneral,OfxImageEffectContextGenerator,OfxImageEffectContextFilter,OfxImageEffectContextTransition,OfxImageEffectContextRetimer,OfxImageEffectContextPaint
supportedPixelDepths=OfxBitDepthByte,OfxBitDepthFloat
supportsMultipleClipDepths=0
supportsMultipleClipPARs=0
supportsSetableFrameRate=0
supportsSetableFielding=0
sequentialRender=0
numCPUs=1
supportsRenderQualityDraft=0
supportsStringAnimation=1
supportsCustomInteract=0
supportsChoiceAnimation=1
supportsStrChoice=0
supportsStrChoiceAnimation=0
supportsBooleanAnimation=1
supportsCustomAnimation=0
supportsParametricParameter=0
supportsParametricAnimation=0
canTransform=0
nativeOrigin=kOfxImageEffectHostPropNativeOriginBottomLeft
osHandle=0
supportsCPURender=1
supportsOpenGLRender=1
supportsOpenCLRender=0
supportsCudaRender=0
supportsCudaStream=0
supportsMetalRender=0
supportsMetalTexture=0
colourManagementStyle=OfxImageEffectColourManagementNone
colourManagementAvailableConfigs=
supportsCamera=0
isMultiPlanar=0
maxParameters=1000
maxPages=0
pageRowCount=0
pageColumnCount=0
supportsProgressSuite=1
supportsTimeLineSuite=1
supportsMessageSuiteV2=1
isNatron=0
supportsDynamicChoices=0
supportsCascadingChoices=0
supportsChannelSelector=0
canDistort=0
supportsInteractKeyModifiers=0
nativeOverlays=
supportedCursors=
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMultiThreadSuite,OfxMessageSuite,OfxMessageSuiteV2,OfxProgressSuite,OfxTimeLineSuite,OfxInteractSuite,OfxImageEffectOpenGLRenderSuite,OfxOpenCLProgramSuite,OfxVegasProgressSuite,OfxVegasStereoscopicImageEffectSuite,OfxVegasKeyframeSuite,OfxHWndInteractSuite

Caveats/Bugs (see https://github.com/NatronGitHub/openfx-misc/issues/66):
- host name: VEGAS Pro 12 and 15 report com.sonycreativesoftware.vegas, VEGAS Pro 19 reports com.vegascreativesoftware.vegas (later versions probably too; not checked). Code that detects VEGAS by host name must check both.
- overlays and custom interacts: VEGAS Pro 12 has no OfxInteractSuite. VEGAS Pro 15 and 19 list OfxInteractSuite and OfxHWndInteractSuite, but report supportsOverlays=0 (so overlays aren't shown) and supportsCustomInteract=0.
- no OfxParametricParameterSuite (supportsParametricParameter=0 in VEGAS Pro 15 and 19): plugins with parametric (curve) parameters cannot be described.
- only RGBA and Alpha components, only 8-bit and float pixel depths (no 16-bit), no multiple clip depths or PARs (VEGAS Pro 12, 15 and 19).
- OpenGL render: VEGAS Pro 15 reports supportsOpenGLRender=0, VEGAS Pro 19 reports supportsOpenGLRender=1 (both list OfxImageEffectOpenGLRenderSuite).
- the render scale in the action arguments is not set on the images (kOfxImageEffectPropRenderScale is always 1 on images, e.g. 0.25 in the inArgs with "Auto" preview quality, but the images are at the inArgs scale). The Support library ignores the image render scale when the host name starts with com.sonycreativesoftware.vegas or (since NatronGitHub/openfx#313) com.vegascreativesoftware.vegas (VEGAS Pro 19; whether VEGAS Pro 19 and later have the same bug is not known). This only turns off the Support library's render scale checks, which exist in debug builds only (without it, they fail the renders of a VEGAS Pro 19 preview); release builds do not check, so their output does not change.
- generator plugins ("Generated Media") are instantiated in the generator context, which Natron never uses for plugins that also support the general context: code paths specific to that context (e.g. a NULL source clip) are only exercised there.
- crashes are usually caught by the VEGAS crash handler: Windows Event Viewer does not log them, and no crash dump is available. The OFX log of debug builds and the "More Information" logs of the crash handler are then the only sources of information. Some crashes skip the handler: a VEGAS Pro 23 crash was logged by Event Viewer (heap corruption, exception 0xc0000374 in ntdll.dll), and a VEGAS Pro 2027 crash showed no crash handler and left nothing in Event Viewer.
- VEGAS Pro 15, 19, 23, 2026 and 2027 were tested with recent openfx-misc builds. There is no host description yet for VEGAS Pro 23, 2026 and 2027:
  - Shadertoy: it appears in none of VEGAS Pro 15, 19, 23, 2026 and 2027, neither as an effect nor as generated media. VEGAS Pro 15 finds no plugin in Shadertoy.ofx (it reports supportsOpenGLRender=0, and Shadertoy needs OpenGL render when it is built without OSMesa). VEGAS Pro 2027 reports that OfxImageEffectActionDescribeInContext fails in all contexts; since only that action fails, VEGAS Pro 2027 very likely reports supportsOpenGLRender=1 (an inference: its host description is not known). VEGAS Pro 19 reports supportsOpenGLRender=1, so the cause there is not known yet.
  - NoOp: works in VEGAS Pro 15, but its "Clip Info..." button does nothing. VEGAS Pro 23, 2026 and 2027 crash when its settings panel is opened (heap corruption in VEGAS Pro 23). These crashes happened with two copies of NoOp installed at once (the Misc bundle and the NoOp bundle), and have not been retested with NoOp alone.
  - VEGAS Pro 2027 is more stable than VEGAS Pro 15 with most plugins that crashed VEGAS Pro 15, but crashes when opening PlasmaCImg, TestGroupsOFX, TestOpenGL and ContactSheetOFX.
  - float-only plugins (all CImg plugins, ContactSheet, LayerContactSheet) misbehaved or crashed. If the host gives them images that are not float, they now fail cleanly instead of writing past the end of the image buffers: render returns kOfxStatFailed and posts a persistent error message that asks to set the project pixel format to 32-bit floating point (the message is cleared by the next successful render).

* Blackmagic Fusion

OFX API version 1.0
hostName=com.blackmagicdesign.Fusion
hostLabel=Fusion
hostVersion=9.0.2 ()
hostIsBackground=0
supportsOverlays=1
supportsMultiResolution=1
supportsTiles=0
temporalClipAccess=1
supportedComponents=OfxImageComponentRGBA
supportedContexts=OfxImageEffectContextGeneral,OfxImageEffectContextRetimer,OfxImageEffectContextTransition,OfxImageEffectContextFilter,OfxImageEffectContextGenerator
supportedPixelDepths=OfxBitDepthByte,OfxBitDepthShort,OfxBitDepthFloat
supportsMultipleClipDepths=1
supportsMultipleClipPARs=1
supportsSetableFrameRate=0
supportsSetableFielding=0
supportsStringAnimation=1
supportsCustomInteract=0
supportsChoiceAnimation=1
supportsBooleanAnimation=1
supportsCustomAnimation=0
supportsParametricAnimation=0
canTransform=0
maxParameters=-1
pageRowCount=-1
pageColumnCount=1
isNatron=0
supportsDynamicChoices=0
supportsCascadingChoices=0
supportsChannelSelector=0
suites=OfxImageEffectSuite,OfxPropertySuite,OfxParameterSuite,OfxMemorySuite,OfxMultiThreadSuite,OfxMessageSuite,OfxProgressSuite,OfxTimeLineSuite

Caveats/Bugs (the first three from openfx-misc commit 8475347f, 2019, when the maintainer tested Fusion 9; see https://github.com/NatronGitHub/openfx-misc/issues/59, which is about Fusion 16):
- kOfxPropAPIVersion kOfxPropVersionLabel kOfxImageEffectInstancePropSequentialRender are not set on the host
- unconnected clips have prop kOfxImageEffectPropPixelDepth=kOfxBitDepthNone
- when scrubbing parameters (changing values fast), the renderScale in the inArgs is 1/3. but the renderScale on images is 3.
- the native origin is top-left (Fusion 9.0.2 is OFX API 1.0, without the kOfxImageEffectHostPropNativeOrigin property of OFX 1.4): the Support library sets ImageEffectHostDescription::nativeOrigin to eNativeOriginTopLeft when the property is missing and the host name is com.eyeonline.Fusion or ends with "Fusion" (ofxsImageEffect.cpp). The property "is only a UI hint, has no impact on pixel processing" (ofxImageEffect.h): it is useful for the coordinates shown by overlays.
- kOfxImagePropPixelAspectRatio may be missing on clips: Clip::getPixelAspectRatio() in the Support library returns 1 when the host gives none (or 0). Source: NatronGitHub/openfx commit fa7951df ("Eyeon Fusion may not define PixelAspectRatio").
- Fusion 16 crashed at startup with the Misc and CImg bundles; fixed in 2019 by NatronGitHub/openfx commit 3effa283 and openfx-misc commit 8475347f. Source: https://github.com/NatronGitHub/openfx-misc/issues/59.
- OpenGL overlays are drawn too small, and DenoiseSharpen's analysis rectangle could not be moved with its overlay (reported by a user). Source: https://github.com/NatronGitHub/openfx-misc/issues/46 and #59.
- (unconfirmed) Fusion Studio 2026: "Reset" on the node copies every visible parameter, read-only ones included, and reports it as a user edit. Source: creativevideotips/cvt-ltx-alpha-gen (https://github.com/creativevideotips/cvt-ltx-alpha-gen), src/ofx/src/plugin.cpp.
- (unconfirmed) see also the Resolve and Fusion 2026 leads in the Resolve section (Resolve and Fusion share the same OFX host).

* Autodesk Toxik

No host description was recorded.
- the native origin is the center: the Support library sets ImageEffectHostDescription::nativeOrigin to eNativeOriginCenter when kOfxImageEffectHostPropNativeOrigin is missing and the host name starts with "Autodesk Toxik" or ends with "Toxik" (ofxsImageEffect.cpp), as the OFX host origin hints describe (http://openeffects.org/standard_changes/host-origin-hints).

* Autodesk Flame

No host description was recorded. Leads for Flame 2027 (2027.1 and 2027.2 on Rocky Linux 9.7), all from creativevideotips/cvt-ltx-alpha-gen (https://github.com/creativevideotips/cvt-ltx-alpha-gen), src/upstream-notes/ofx.md:
- (unconfirmed) every OFX action, render included, runs on Flame's main UI thread.
- (unconfirmed) a plug-in's paramSetValue comes straight back as kOfxActionInstanceChanged with kOfxChangePluginEdited; a plug-in that reacted to it by setting values again recursed until Flame crashed. Handle only kOfxChangeUserEdited, or guard against re-entrance.
- (unconfirmed) kOfxPluginPropParamPageOrder is unknown, so the Support library's setPageParamOrder() threw (NatronGitHub/openfx#351 makes it non-throwing).
- (unconfirmed) an Alpha-only output requested in getClipPreferences is ignored: the output stays RGBA.

* FilmLight Baselight

No host description was recorded.
- (unconfirmed) suspected: CPU images always have a negative row bytes (kOfxImagePropRowBytes), because Baselight stores them top-down. Source: ntsc-rs/ntsc-rs#600, where the developer of Baselight's OFX host code gives it as a suspicion ("My suspicion is that..."); the ntsc-rs author then fixed a bug in its negative row bytes handling.
- (unconfirmed) a region of interest equal to the source RoD makes Baselight log "requests infinite ROI for Source; using expanded working format", which may cost performance. Source: the log in ntsc-rs/ntsc-rs#600.
- (unconfirmed) see also "Several hosts" (animated choices, clips at project load).

* Assimilate Scratch

No host description was recorded.
- (unconfirmed) see "Several hosts" (animated choices, clips at project load).

* Boris FX Silhouette

No host description was recorded.
- (unconfirmed) see "Several hosts" (animated choices, clips at project load).

* FXhome HitFilm

No host description was recorded.
- (unconfirmed) kOfxActionDestroyInstance is only called when the application closes, not when the effect is removed, so the memory of removed instances is not released until then. Source: AcademySoftwareFoundation/openfx#80.

* VEGAS Pro 17 and later (lead to move into the VEGAS section above, which NatronGitHub/openfx-misc#416 edits)

- (unconfirmed) red and blue are swapped in some plug-ins' output. Sources: ntsc-rs/ntsc-rs#14 (VEGAS Pro 17); https://revisionfx.com/faq/vegas/.
- (unconfirmed) VEGAS used to skip parameter types it does not support silently, instead of returning an error when they are defined. Source: a comment by Revision FX in AcademySoftwareFoundation/openfx#286.
