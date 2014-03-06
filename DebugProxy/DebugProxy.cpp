/*
 OFX DebugProxy plugin.
 Intercept and debug communication between an OFX host and an OFX plugin.

 Copyright (C) 2013 INRIA
 Author: Frederic Devernay <frederic.devernay@inria.fr>

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France


 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.

 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England
 
 */

// the clip support code may be wrong. in cas of crash, it can be easily disactivated
#define OFX_DEBUG_PROXY_CLIPS

#ifdef WIN32
#include <windows.h>
#endif
 
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <new>
#include <cassert>
#include <map>
#include <list>
#include <string>

#include "ofxImageEffect.h"
#include "ofxProgress.h"
#include "ofxTimeLine.h"
#include "ofxParametricParam.h"
#ifdef OFX_EXTENSIONS_VEGAS
#include "ofxSonyVegas.h"
#endif
#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/camera.h"
#include "nuke/fnOfxExtensions.h"
#include "nuke/fnPublicOfxExtensions.h"
#endif
#include "ofxhBinary.h"
#include "ofxhUtilities.h"


// the Plugin path can be set here in the source code, or at runtime via the OFX_DEBUGPROXY_BINARY environment variable
#ifndef BINARY_PATH
#if defined(WINDOWS)
#define BINARY_PATH "C:\\Program Files\\Common Files\\OFX\\Plugins.disabled\\Sapphire.ofx.bundle\\Contents\\Win64\\Sapphire.ofx"
#endif
#if defined(__linux__)
#define BINARY_PATH "/usr/OFX/Plugins.disabled/Sapphire.ofx.bundle/Contents/Linux-x86-64/Sapphire.ofx"
#endif
#if defined(__APPLE__)
#define BINARY_PATH "/Library/OFX/Plugins.disabled/Sapphire.ofx.bundle/Contents/MacOS/Sapphire.ofx"
#endif
#endif

typedef void     (OfxSetHost)(OfxHost *);

typedef OfxStatus (*OfxImageEffectSuiteV1getPropertySet)(OfxImageEffectHandle imageEffect, OfxPropertySetHandle *propHandle);
typedef OfxStatus (*OfxImageEffectSuiteV1getParamSet)(OfxImageEffectHandle imageEffect, OfxParamSetHandle *paramSet);
typedef OfxStatus (*OfxImageEffectSuiteV1clipDefine)(OfxImageEffectHandle imageEffect, const char *name, OfxPropertySetHandle *propertySet);
typedef OfxStatus (*OfxImageEffectSuiteV1clipGetHandle)(OfxImageEffectHandle imageEffect, const char *name, OfxImageClipHandle *clip, OfxPropertySetHandle *propertySet);
typedef OfxStatus (*OfxImageEffectSuiteV1clipGetPropertySet)(OfxImageClipHandle clip, OfxPropertySetHandle *propHandle);
typedef OfxStatus (*OfxImageEffectSuiteV1clipGetImage)(OfxImageClipHandle clip, OfxTime time, OfxRectD *region, OfxPropertySetHandle *imageHandle);
typedef OfxStatus (*OfxImageEffectSuiteV1clipReleaseImage)(OfxPropertySetHandle imageHandle);
typedef OfxStatus (*OfxImageEffectSuiteV1clipGetRegionOfDefinition)(OfxImageClipHandle clip, OfxTime time, OfxRectD *bounds);
typedef int (*OfxImageEffectSuiteV1abort)(OfxImageEffectHandle imageEffect);
typedef OfxStatus (*OfxImageEffectSuiteV1imageMemoryAlloc)(OfxImageEffectHandle instanceHandle, size_t nBytes, OfxImageMemoryHandle *memoryHandle);
typedef OfxStatus (*OfxImageEffectSuiteV1imageMemoryFree)(OfxImageMemoryHandle memoryHandle);
typedef OfxStatus (*OfxImageEffectSuiteV1imageMemoryLock)(OfxImageMemoryHandle memoryHandle, void **returnedPtr);
typedef OfxStatus (*OfxImageEffectSuiteV1imageMemoryUnlock)(OfxImageMemoryHandle memoryHandle);

static OfxImageEffectSuiteV1getPropertySet getPropertySetNthFunc(int nth);
static OfxImageEffectSuiteV1getParamSet getParamSetNthFunc(int nth);
static OfxImageEffectSuiteV1clipDefine clipDefineNthFunc(int nth);
static OfxImageEffectSuiteV1clipGetHandle clipGetHandleNthFunc(int nth);
static OfxImageEffectSuiteV1clipGetPropertySet clipGetPropertySetNthFunc(int nth);
static OfxImageEffectSuiteV1clipGetImage clipGetImageNthFunc(int nth);
static OfxImageEffectSuiteV1clipReleaseImage clipReleaseImageNthFunc(int nth);
static OfxImageEffectSuiteV1clipGetRegionOfDefinition clipGetRegionOfDefinitionNthFunc(int nth);
static OfxImageEffectSuiteV1abort abortNthFunc(int nth);
static OfxImageEffectSuiteV1imageMemoryAlloc imageMemoryAllocNthFunc(int nth);
static OfxImageEffectSuiteV1imageMemoryFree imageMemoryFreeNthFunc(int nth);
static OfxImageEffectSuiteV1imageMemoryLock imageMemoryLockNthFunc(int nth);
static OfxImageEffectSuiteV1imageMemoryUnlock imageMemoryUnlockNthFunc(int nth);

/** @brief A class that lists all the properties of a host */
struct ImageEffectHostDescription {
 public :
    std::vector<int> apiVersion; // TODO
    std::string type;
  std::string hostName;
    std::string hostLabel;// TODO
    std::vector<int> version;// TODO
    std::string versionLabel;// TODO
  bool hostIsBackground;
  bool supportsOverlays;
  bool supportsMultiResolution;
  bool supportsTiles;
  bool temporalClipAccess;
  std::vector<std::string> _supportedComponents;
  std::vector<std::string> _supportedContexts;
  std::vector<std::string> _supportedPixelDepths;


  bool supportsMultipleClipDepths;
  bool supportsMultipleClipPARs;
  bool supportsSetableFrameRate;
  bool supportsSetableFielding;
  bool supportsStringAnimation;
  bool supportsCustomInteract;
  bool supportsChoiceAnimation;
  bool supportsBooleanAnimation;
  bool supportsCustomAnimation;
  bool supportsParametricAnimation;
#ifdef OFX_EXTENSIONS_NUKE
  bool canTransform;
#endif
  int maxParameters;
  int maxPages;
  int pageRowCount;
  int pageColumnCount;
};

// pointers to various bits of the host
static std::vector<OfxHost*>                          gHost;
static std::vector<OfxHost>                           gProxy;
static std::vector<ImageEffectHostDescription>        gHostDescription;
static std::vector<OfxImageEffectSuiteV1*>            gEffectHost;
static std::vector<OfxImageEffectSuiteV1>             gEffectProxy;
static std::vector<OfxPropertySuiteV1*>               gPropHost;
static std::vector<OfxParameterSuiteV1*>              gParamHost;
static std::vector<OfxMemorySuiteV1*>                 gMemoryHost;
static std::vector<OfxMultiThreadSuiteV1*>            gThreadHost;
static std::vector<OfxMessageSuiteV1*>                gMessageHost;
static std::vector<OfxMessageSuiteV2*>                gMessageV2Host;
static std::vector<OfxProgressSuiteV1*>               gProgressHost;
static std::vector<OfxTimeLineSuiteV1*>               gTimeLineHost;
static std::vector<OfxParametricParameterSuiteV1*>    gParametricParameterHost;
#ifdef OFX_EXTENSIONS_NUKE
static std::vector<NukeOfxCameraSuiteV1*>             gCameraHost;
static std::vector<FnOfxImageEffectPlaneSuiteV1*>     gImageEffectPlaneHost;
static std::vector<FnOfxImageEffectPlaneSuiteV2*>     gImageEffectPlaneV2Host;
#endif
#ifdef OFX_EXTENSIONS_VEGAS
static std::vector<OfxVegasProgressSuiteV1*>          gVegasProgressHost;
static std::vector<OfxVegasStereoscopicImageSuiteV1*> gVegasStereoscopicImageHost;
static std::vector<OfxVegasKeyframeSuiteV1*>          gVegasKeyframeHost;
#endif

static std::vector<OfxInteractSuiteV1*>    gInteractHost;

#ifdef OFX_DEBUG_PROXY_CLIPS
// for each plugin, we store a map form the context to the list of defined clips
// obviously, it should be made thread-safe by the use of a mutex.
static std::vector<std::map<std::string, std::list<std::string> > > gClips;

// maps context descriptors to contexts (set by OfxImageEffectActionDescribeInContext, used in clipDefine)
static std::map<OfxImageEffectHandle, std::string> gContexts;
#endif

////////////////////////////////////////////////////////////////////////////////
// the plugin struct 
static const char* gBinaryPath = 0;
static OFX::Binary *gBinary = 0;
static std::vector<OfxPlugin> gPlugins;
static int gPluginsNb = 0;
static std::vector<OfxPluginEntryPoint*> gPluginsMainEntry;
static std::vector<OfxPluginEntryPoint*> gPluginsOverlayMain;
static int (*OfxGetNumberOfPlugins_binary)(void) = 0;
static OfxPlugin* (*OfxGetPlugin_binary)(int) = 0;
static std::vector<OfxSetHost*> gPluginsSetHost;

// load the underlying binary
struct Loader {
    Loader()
    {
        if (gBinary) {
            assert(OfxGetNumberOfPlugins_binary);
            assert(OfxGetPlugin_binary);
            return;
        }
        gBinaryPath = std::getenv("OFX_DEBUGPROXY_BINARY");
        if (gBinaryPath == NULL) {
            gBinaryPath = BINARY_PATH;
        }
        if (gBinaryPath) {
            gBinary = new OFX::Binary(gBinaryPath);
        }
        if (gBinary) {
            gBinary->load();
            // fetch the binary entry points
            OfxGetNumberOfPlugins_binary = (int(*)()) gBinary->findSymbol("OfxGetNumberOfPlugins");
            OfxGetPlugin_binary = (OfxPlugin*(*)(int)) gBinary->findSymbol("OfxGetPlugin");
            std::cout << "OFX DebugProxy: " << gBinaryPath << " loaded" << std::endl;
        }
    }

    ~Loader()
    {
        if (gBinary) {
            gBinary->unload();
            delete gBinary;
        }
        gBinary = 0;
        OfxGetNumberOfPlugins_binary = 0;
        OfxGetPlugin_binary = 0;
        std::cout << "OFX DebugProxy: " << gBinaryPath << " unloaded" << std::endl;
    }
};

static Loader load;

/* fetch our host APIs from the host struct given us
 the plugin's set host function must have been already called
 */
inline OfxStatus
fetchHostSuites(int nth)
{
    assert(nth < gHost.size());
    if(!gHost[nth])
        return kOfxStatErrMissingHostFeature;

    if (nth+1 > gEffectHost.size()) {
        gEffectHost.resize(nth+1);
        gEffectProxy.resize(nth+1);
        gPropHost.resize(nth+1);
        gParamHost.resize(nth+1);
        gMemoryHost.resize(nth+1);
        gThreadHost.resize(nth+1);
        gMessageHost.resize(nth+1);
        gMessageV2Host.resize(nth+1);
        gProgressHost.resize(nth+1);
        gTimeLineHost.resize(nth+1);
        gParametricParameterHost.resize(nth+1);
#ifdef OFX_EXTENSIONS_NUKE
        gCameraHost.resize(nth+1);
        gImageEffectPlaneHost.resize(nth+1);
        gImageEffectPlaneV2Host.resize(nth+1);
#endif
#ifdef OFX_EXTENSIONS_VEGAS
        gVegasProgressHost.resize(nth+1);
        gVegasStereoscopicImageHost.resize(nth+1);
        gVegasKeyframeHost.resize(nth+1);
#endif

        gInteractHost.resize(nth+1);
#ifdef OFX_DEBUG_PROXY_CLIPS
        gClips.resize(nth+1);
#endif
    }

    gEffectHost[nth]                 = (OfxImageEffectSuiteV1 *) gHost[nth]->fetchSuite(gHost[nth]->host, kOfxImageEffectSuite, 1);
    gPropHost[nth]                   = (OfxPropertySuiteV1 *)    gHost[nth]->fetchSuite(gHost[nth]->host, kOfxPropertySuite, 1);
    gParamHost[nth]                  = (OfxParameterSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxParameterSuite, 1);
    gMemoryHost[nth]                 = (OfxMemorySuiteV1 *)      gHost[nth]->fetchSuite(gHost[nth]->host, kOfxMemorySuite, 1);
    gThreadHost[nth]                 = (OfxMultiThreadSuiteV1 *) gHost[nth]->fetchSuite(gHost[nth]->host, kOfxMultiThreadSuite, 1);
    gMessageHost[nth]                = (OfxMessageSuiteV1 *)     gHost[nth]->fetchSuite(gHost[nth]->host, kOfxMessageSuite, 1);
    gMessageV2Host[nth]              = (OfxMessageSuiteV2 *)     gHost[nth]->fetchSuite(gHost[nth]->host, kOfxMessageSuite, 2);
    gProgressHost[nth]               = (OfxProgressSuiteV1 *)    gHost[nth]->fetchSuite(gHost[nth]->host, kOfxProgressSuite, 1);
    gTimeLineHost[nth]               = (OfxTimeLineSuiteV1 *)    gHost[nth]->fetchSuite(gHost[nth]->host, kOfxTimeLineSuite, 1);
    gParametricParameterHost[nth]    = (OfxParametricParameterSuiteV1 *) gHost[nth]->fetchSuite(gHost[nth]->host, kOfxParametricParameterSuite, 1);
#ifdef OFX_EXTENSIONS_NUKE
    gCameraHost[nth]                 = (NukeOfxCameraSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kNukeOfxCameraSuite, 1);
    gImageEffectPlaneHost[nth]       = (FnOfxImageEffectPlaneSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kFnOfxImageEffectPlaneSuite, 1);
    gImageEffectPlaneV2Host[nth]     = (FnOfxImageEffectPlaneSuiteV2 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kFnOfxImageEffectPlaneSuite, 2);
#endif
#ifdef OFX_EXTENSIONS_VEGAS
    gVegasProgressHost[nth]          = (OfxVegasProgressSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxVegasProgressSuite, 1);
    gVegasStereoscopicImageHost[nth] = (OfxVegasStereoscopicImageSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxVegasStereoscopicImageEffectSuite, 1);
    gVegasKeyframeHost[nth]          = (OfxVegasKeyframeSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxVegasKeyframeSuite, 1);
#endif
    gInteractHost[nth]   = (OfxInteractSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxInteractSuite, 1);
    if(!gEffectHost[nth] || !gPropHost[nth] || !gParamHost[nth] || !gMemoryHost[nth] || !gThreadHost[nth])
        return kOfxStatErrMissingHostFeature;
    // setup proxies
    gEffectProxy[nth] = *gEffectHost[nth];
    gEffectProxy[nth].getPropertySet = getPropertySetNthFunc(nth);
    gEffectProxy[nth].getParamSet = getParamSetNthFunc(nth);
    gEffectProxy[nth].clipDefine = clipDefineNthFunc(nth);
    gEffectProxy[nth].clipGetHandle = clipGetHandleNthFunc(nth);
    gEffectProxy[nth].clipGetPropertySet = clipGetPropertySetNthFunc(nth);
    gEffectProxy[nth].clipGetImage = clipGetImageNthFunc(nth);
    gEffectProxy[nth].clipReleaseImage = clipReleaseImageNthFunc(nth);
    gEffectProxy[nth].clipGetRegionOfDefinition = clipGetRegionOfDefinitionNthFunc(nth);
    gEffectProxy[nth].abort = abortNthFunc(nth);
    gEffectProxy[nth].imageMemoryAlloc = imageMemoryAllocNthFunc(nth);
    gEffectProxy[nth].imageMemoryFree = imageMemoryFreeNthFunc(nth);
    gEffectProxy[nth].imageMemoryLock = imageMemoryLockNthFunc(nth);
    gEffectProxy[nth].imageMemoryUnlock = imageMemoryUnlockNthFunc(nth);
    return kOfxStatOK;
}

inline OfxStatus
fetchHostDescription(int nth)
{
    assert(nth < gHost.size());
    if(!gHost[nth])
        return kOfxStatErrMissingHostFeature;

    if (nth+1 > gHostDescription.size()) {
        gHostDescription.resize(nth+1);
    }

    OfxPropertySetHandle host = gHost[nth]->host;
    ImageEffectHostDescription &hostDesc = gHostDescription[nth];
    OfxStatus st;
    // and get some properties
    char *name;
    st = gPropHost[nth]->propGetString(host, kOfxPropName, 0, &name);
    assert(st == kOfxStatOK);
    hostDesc.hostName                   = name;

    {
        int hostIsBackground;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectHostPropIsBackground, 0, &hostIsBackground);
        hostDesc.hostIsBackground = (st == kOfxStatOK) && hostIsBackground != 0;
    }
    {
        int supportsOverlays;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropSupportsOverlays, 0, &supportsOverlays);
        hostDesc.supportsOverlays = (st == kOfxStatOK) && supportsOverlays != 0;
    }
    {
        int supportsMultiResolution;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropSupportsMultiResolution, 0, &supportsMultiResolution);
        hostDesc.supportsMultiResolution = (st == kOfxStatOK) && supportsMultiResolution != 0;
    }
    {
        int supportsTiles;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropSupportsTiles, 0, &supportsTiles);
        hostDesc.supportsTiles = (st == kOfxStatOK) && supportsTiles != 0;
    }
    {
        int temporalClipAccess;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropTemporalClipAccess, 0, &temporalClipAccess);
        hostDesc.temporalClipAccess = (st == kOfxStatOK) && temporalClipAccess != 0;
    }
    int numComponents;
    st = gPropHost[nth]->propGetDimension(host, kOfxImageEffectPropSupportedComponents, &numComponents);
    assert (st == kOfxStatOK);
    for(int i=0; i<numComponents; ++i) {
        char *comp;
        st = gPropHost[nth]->propGetString(host, kOfxImageEffectPropSupportedComponents, i, &comp);
        assert (st == kOfxStatOK);
        hostDesc._supportedComponents.push_back(comp);
    }
    int numContexts;
    st = gPropHost[nth]->propGetDimension(host, kOfxImageEffectPropSupportedContexts, &numContexts);
    assert (st == kOfxStatOK);
    for(int i=0; i<numContexts; ++i) {
        char* cont;
        st = gPropHost[nth]->propGetString(host, kOfxImageEffectPropSupportedContexts, i, &cont);
        assert (st == kOfxStatOK);
        hostDesc._supportedContexts.push_back(cont);
    }
	int numPixelDepths;
    st = gPropHost[nth]->propGetDimension(host, kOfxImageEffectPropSupportedPixelDepths, &numPixelDepths);
    assert (st == kOfxStatOK);
    for(int i=0; i<numPixelDepths; ++i) {
        char *depth;
        st = gPropHost[nth]->propGetString(host, kOfxImageEffectPropSupportedPixelDepths, i, &depth);
        assert (st == kOfxStatOK);
        hostDesc._supportedPixelDepths.push_back(depth);
    }
    {
        int supportsMultipleClipDepths;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectHostPropIsBackground, 0, &supportsMultipleClipDepths);
        hostDesc.supportsMultipleClipDepths = (st == kOfxStatOK) && supportsMultipleClipDepths != 0;
    }
    {
        int supportsMultipleClipPARs;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropSupportsMultipleClipPARs, 0, &supportsMultipleClipPARs);
        hostDesc.supportsMultipleClipPARs = (st == kOfxStatOK) && supportsMultipleClipPARs != 0;
    }
    {
        int supportsSetableFrameRate;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropSetableFrameRate, 0, &supportsSetableFrameRate);
        hostDesc.supportsSetableFrameRate = (st == kOfxStatOK) && supportsSetableFrameRate != 0;
    }
    {
        int supportsSetableFielding;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropSetableFielding, 0, &supportsSetableFielding);
        hostDesc.supportsSetableFielding = (st == kOfxStatOK) && supportsSetableFielding != 0;
    }
    {
        int supportsStringAnimation;
        st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropSupportsStringAnimation, 0, &supportsStringAnimation);
        hostDesc.supportsStringAnimation = (st == kOfxStatOK) && supportsStringAnimation != 0;
    }
    {
        int supportsCustomInteract;
        st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropSupportsCustomInteract, 0, &supportsCustomInteract);
        hostDesc.supportsCustomInteract = (st == kOfxStatOK) && supportsCustomInteract != 0;
    }
    {
        int supportsChoiceAnimation;
        st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropSupportsChoiceAnimation, 0, &supportsChoiceAnimation);
        hostDesc.supportsChoiceAnimation = (st == kOfxStatOK) && supportsChoiceAnimation != 0;
    }
    {
        int supportsBooleanAnimation;
        st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropSupportsBooleanAnimation, 0, &supportsBooleanAnimation);
        hostDesc.supportsBooleanAnimation = (st == kOfxStatOK) && supportsBooleanAnimation != 0;
    }
    {
        int supportsCustomAnimation;
        st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropSupportsCustomAnimation, 0, &supportsCustomAnimation);
        hostDesc.supportsCustomAnimation = (st == kOfxStatOK) && supportsCustomAnimation != 0;
    }
    {
        int supportsParametricAnimation;
        st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropSupportsParametricAnimation, 0, &supportsParametricAnimation);
        hostDesc.supportsParametricAnimation = (st == kOfxStatOK) && supportsParametricAnimation != 0;
    }
#ifdef OFX_EXTENSIONS_NUKE
    {
        int canTransform;
        st = gPropHost[nth]->propGetInt(host, kFnOfxImageEffectCanTransform, 0, &canTransform);
        hostDesc.canTransform = (st == kOfxStatOK) && canTransform != 0;
    }

#endif
    st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropMaxParameters, 0, &hostDesc.maxParameters);
    assert (st == kOfxStatOK);
    st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropMaxPages, 0, &hostDesc.maxPages);
    assert (st == kOfxStatOK);
    st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropPageRowColumnCount, 0, &hostDesc.pageRowCount);
    assert (st == kOfxStatOK);
    st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropPageRowColumnCount, 1, &hostDesc.pageColumnCount);
    assert (st == kOfxStatOK);

    return kOfxStatOK;
}

inline void
printHostDescription(int nth)
{
    const ImageEffectHostDescription &hostDesc = gHostDescription[nth];
    std::cout << "OFX DebugProxy: host description follows" << std::endl;
    std::cout << "hostName=" << hostDesc.hostName << std::endl;
    std::cout << "hostIsBackground=" << hostDesc.hostIsBackground << std::endl;
    std::cout << "supportsOverlays=" << hostDesc.supportsOverlays << std::endl;
    std::cout << "supportsMultiResolution=" << hostDesc.supportsMultiResolution << std::endl;
    std::cout << "supportsTiles=" << hostDesc.supportsTiles << std::endl;
    std::cout << "temporalClipAccess=" << hostDesc.temporalClipAccess << std::endl;
    bool first;
    first = true;
    std::cout << "supportedComponents=";
    for (std::vector<std::string>::const_iterator it = hostDesc._supportedComponents.begin(); it != hostDesc._supportedComponents.end(); ++it) {
        if (!first) {
            std::cout << ",";
        }
        first = false;
        std::cout << *it;
    }
    std::cout << std::endl;
    first = true;
    std::cout << "supportedContexts=";
    for (std::vector<std::string>::const_iterator it = hostDesc._supportedContexts.begin(); it != hostDesc._supportedContexts.end(); ++it) {
        if (!first) {
            std::cout << ",";
        }
        first = false;
        std::cout << *it;
    }
    std::cout << std::endl;
    first = true;
    std::cout << "supportedPixelDepths=";
    for (std::vector<std::string>::const_iterator it = hostDesc._supportedPixelDepths.begin(); it != hostDesc._supportedPixelDepths.end(); ++it) {
        if (!first) {
            std::cout << ",";
        }
        first = false;
        std::cout << *it;
    }
    std::cout << std::endl;
    std::cout << "supportsMultipleClipDepths=" << hostDesc.supportsMultipleClipDepths << std::endl;
    std::cout << "supportsMultipleClipPARs=" << hostDesc.supportsMultipleClipPARs << std::endl;
    std::cout << "supportsSetableFrameRate=" << hostDesc.supportsSetableFrameRate << std::endl;
    std::cout << "supportsSetableFielding=" << hostDesc.supportsSetableFielding << std::endl;
    std::cout << "supportsStringAnimation=" << hostDesc.supportsStringAnimation << std::endl;
    std::cout << "supportsCustomInteract=" << hostDesc.supportsCustomInteract << std::endl;
    std::cout << "supportsChoiceAnimation=" << hostDesc.supportsChoiceAnimation << std::endl;
    std::cout << "supportsBooleanAnimation=" << hostDesc.supportsBooleanAnimation << std::endl;
    std::cout << "supportsCustomAnimation=" << hostDesc.supportsCustomAnimation << std::endl;
    std::cout << "supportsParametricAnimation=" << hostDesc.supportsParametricAnimation << std::endl;
#ifdef OFX_EXTENSIONS_NUKE
    std::cout << "canTransform=" << hostDesc.canTransform << std::endl;
#endif
    std::cout << "maxParameters=" << hostDesc.maxParameters << std::endl;
    std::cout << "pageRowCount=" << hostDesc.pageRowCount << std::endl;
    std::cout << "pageColumnCount=" << hostDesc.pageColumnCount << std::endl;
    std::cout << "suites=";
    if (gEffectHost[nth]) {
        std::cout << kOfxImageEffectSuite << ',';
    }
    if (gPropHost[nth]) {
        std::cout << kOfxPropertySuite << ',';
    }
    if (gParamHost[nth]) {
        std::cout << kOfxParameterSuite << ',';
    }
    if (gMemoryHost[nth]) {
        std::cout << kOfxMemorySuite << ',';
    }
    if (gMessageHost[nth]) {
        std::cout << kOfxMessageSuite << ',';
    }
    if (gMessageV2Host[nth]) {
        std::cout << kOfxMessageSuite << "V2" << ',';
    }
    if (gProgressHost[nth]) {
        std::cout << kOfxProgressSuite << ',';
    }
    if (gTimeLineHost[nth]) {
        std::cout << kOfxTimeLineSuite << ',';
    }
    if (gParametricParameterHost[nth]) {
        std::cout << kOfxParametricParameterSuite << ',';
    }
#ifdef OFX_EXTENSIONS_NUKE
    if (gCameraHost[nth]) {
        std::cout << kNukeOfxCameraSuite << ',';
    }
    if (gImageEffectPlaneHost[nth]) {
        std::cout << kFnOfxImageEffectPlaneSuite << ',';
    }
    if (gImageEffectPlaneV2Host[nth]) {
        std::cout << kFnOfxImageEffectPlaneSuite << "V2" << ',';
    }
#endif
#ifdef OFX_EXTENSIONS_VEGAS
    if (gVegasProgressHost[nth]) {
        std::cout << kOfxVegasProgressSuite << ',';
    }
    if (gVegasStereoscopicImageHost[nth]) {
        std::cout << kOfxVegasStereoscopicImageEffectSuite << ',';
    }
    if (gVegasKeyframeHost[nth]) {
        std::cout << kOfxVegasKeyframeSuite << ',';
    }
#endif
    std::cout << std::endl;
    std::cout << "OFX DebugProxy: host description finished" << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
// the entry point for the overlay
static OfxStatus
overlayMain(int nth, const char *action, const void *handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
  std::stringstream ss;
    ss << gPlugins[nth].pluginIdentifier << ".i." << action;
  std::stringstream ssr;
  OfxStatus st = kOfxStatErrUnknown;
  try {
    // pre-hooks on some actions (e.g. print or modify parameters)
    if(strcmp(action, kOfxActionDescribe) == 0) {
      // no inArgs

      ss << "(" << handle << ")";
    }
    else if(strcmp(action, kOfxActionCreateInstance) == 0) {
      // no inArgs

      ss << "(" << handle << ")";
    } 
    else if(strcmp(action, kOfxActionDestroyInstance) == 0) {
      // no inArgs

      ss << "(" << handle << ")";
    } 
    else if(strcmp(action, kOfxInteractActionDraw) == 0) {
      // inArgs has the following properties on an image effect plugin,
      //     kOfxPropEffectInstance - a handle to the effect for which the interact has been,
      //     kOfxInteractPropViewportSize - the openGL viewport size for the instance
      //     kOfxInteractPropPixelScale - the scale factor to convert cannonical pixels to screen pixels
      //     kOfxInteractPropBackgroundColour - the background colour of the application behind the current view
      //     kOfxPropTime - the effect time at which changed occured
      //     kOfxImageEffectPropRenderScale - the render scale applied to any image fetched
      
      ss << "(" << handle << ",TODO)";
    }
    else if(strcmp(action, kOfxInteractActionPenMotion) == 0 ||
            strcmp(action, kOfxInteractActionPenDown) == 0 ||
            strcmp(action, kOfxInteractActionPenUp) == 0) {
      // inArgs has the following properties on an image effect plugin,

      //     kOfxPropEffectInstance - a handle to the effect for which the interact has been,
      //     kOfxInteractPropViewportSize - the openGL viewport size for the instance
      //     kOfxInteractPropPixelScale - the scale factor to convert cannonical pixels to screen pixels
      //     kOfxInteractPropBackgroundColour - the background colour of the application behind the current view
      //     kOfxPropTime - the effect time at which changed occured
      //     kOfxImageEffectPropRenderScale - the render scale applied to any image fetched
      //     kOfxInteractPropPenPosition - postion of the pen in,
      //     kOfxInteractPropPenViewportPosition - postion of the pen in,
      //     kOfxInteractPropPenPressure - the pressure of the pen,

      ss << "(" << handle << ",TODO)";
    }
    else if(strcmp(action, kOfxInteractActionKeyDown) == 0 ||
            strcmp(action, kOfxInteractActionKeyUp) == 0 ||
            strcmp(action, kOfxInteractActionKeyRepeat) == 0) {
      // inArgs has the following properties on an image effect plugin,
      //     kOfxPropEffectInstance - a handle to the effect for which the interact has been,
      //     kOfxPropKeySym - single integer value representing the key that was manipulated, this may not have a UTF8 representation (eg: a return key)
      //     kOfxPropKeyString - UTF8 string representing a character key that was pressed, some keys have no UTF8 encoding, in which case this is ""
      //     kOfxPropTime - the effect time at which changed occured
      //     kOfxImageEffectPropRenderScale - the render scale applied to any image fetched

      ss << "(" << handle << ",TODO)";
    }
    else if(strcmp(action, kOfxInteractActionGainFocus) == 0 ||
            strcmp(action, kOfxInteractActionLoseFocus) == 0) {
      // inArgs has the following properties on an image effect plugin,
      //     kOfxPropEffectInstance - a handle to the effect for which the interact is being used on,
      //     kOfxInteractPropViewportSize - the openGL viewport size for the instance,
      //     kOfxInteractPropPixelScale - the scale factor to convert cannonical pixels to screen pixels,
      //     kOfxInteractPropBackgroundColour - the background colour of the application behind the current view
      //     kOfxPropTime - the effect time at which changed occured
      //     kOfxImageEffectPropRenderScale - the render scale applied to any image fetched

      ss << "(" << handle << ",TODO)";
    }
    else {
      // unknown OFX Action
      ss << "(" << handle << ") [UNKNOWN ACTION]";
    }


    std::cout << "OFX DebugProxy: " << ss.str() << std::endl;

    st =  gPluginsOverlayMain[nth](action, handle, inArgs, outArgs);

    // post-hooks on some actions (e.g. print or modify result)
    // get the outargs
    if(strcmp(action, kOfxActionDescribe) == 0) {
      // no outArgs
    }
    else if(strcmp(action, kOfxActionCreateInstance) == 0) {
      // no outArgs
    } 
    else if(strcmp(action, kOfxActionDestroyInstance) == 0) {
      // no outArgs
    } 
    else if(strcmp(action, kOfxInteractActionDraw) == 0) {
      // no outArgs
    } 
    else if(strcmp(action, kOfxInteractActionPenMotion) == 0 ||
            strcmp(action, kOfxInteractActionPenDown) == 0 ||
            strcmp(action, kOfxInteractActionPenUp) == 0) {
      // no outArgs
    }
    else if(strcmp(action, kOfxInteractActionKeyDown) == 0 ||
            strcmp(action, kOfxInteractActionKeyUp) == 0 ||
            strcmp(action, kOfxInteractActionKeyRepeat) == 0) {
      // no outArgs
    }
    else if(strcmp(action, kOfxInteractActionGainFocus) == 0 ||
            strcmp(action, kOfxInteractActionLoseFocus) == 0) {
      // no outArgs
    }

  } catch (std::bad_alloc) {
    // catch memory
    std::cout << "OFX DebugProxy Plugin Memory error." << std::endl;
    return kOfxStatErrMemory;
  } catch ( const std::exception& e ) {
    // standard exceptions
    std::cout << "OFX DebugProxy Plugin error: " << e.what() << std::endl;
    return kOfxStatErrUnknown;
  } catch (int err) {
    // ho hum, gone wrong somehow
    std::cout << "OFX DebugProxy Plugin error: " << err << std::endl;
    return err;
  } catch ( ... ) {
    // everything else
    std::cout << "OFX DebugProxy Plugin error" << std::endl;
    return kOfxStatErrUnknown;
  }
  
  if (ssr.str().empty()) {
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << ".i." << action << "->" << OFX::StatStr(st) << std::endl;
  } else {
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << ".i." << action << "->" << OFX::StatStr(st) << ": " << ssr.str() << std::endl;
  }

  return st;
}

template<int nth>
static OfxStatus
overlayMainNth(const char *action,  const void *handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
  return overlayMain(nth, action, handle, inArgs, outArgs);
}

#define NTHFUNC10(nth) \
NTHFUNC(nth); \
NTHFUNC(nth+1); \
NTHFUNC(nth+2); \
NTHFUNC(nth+3); \
NTHFUNC(nth+4); \
NTHFUNC(nth+5); \
NTHFUNC(nth+6); \
NTHFUNC(nth+7); \
NTHFUNC(nth+8); \
NTHFUNC(nth+9)

#define NTHFUNC100(nth) \
NTHFUNC10(nth); \
NTHFUNC10(nth+10); \
NTHFUNC10(nth+20); \
NTHFUNC10(nth+30); \
NTHFUNC10(nth+40); \
NTHFUNC10(nth+50); \
NTHFUNC10(nth+60); \
NTHFUNC10(nth+70); \
NTHFUNC10(nth+80); \
NTHFUNC10(nth+90)

#define NTHFUNC(nth) \
case nth: \
return overlayMainNth<nth>

static OfxPluginEntryPoint*
overlayMainNthFunc(int nth)
{
  switch (nth) {
    NTHFUNC100(0);
    NTHFUNC100(100);
    NTHFUNC100(200);
  }
  std::cout << "OFX DebugProxy: Error: cannot create overlay entry point for plugin " << nth << std::endl;
  return 0;
}
#undef NTHFUNC

#ifdef OFX_DEBUG_PROXY_CLIPS
static std::string getContext(int nth, OfxImageEffectHandle handle)
{
    // fetch effect props
    OfxPropertySetHandle propHandle;
    OfxStatus st = gEffectHost[nth]->getPropertySet((OfxImageEffectHandle)handle, &propHandle);
    assert(st == kOfxStatOK);
    // get context
    char *context;
    st = gPropHost[nth]->propGetString(propHandle, kOfxImageEffectPropContext, 0, &context);
    assert(st == kOfxStatOK);
    return context;
}
#endif

////////////////////////////////////////////////////////////////////////////////
// The main function
static OfxStatus
pluginMain(int nth, const char *action, const void *handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
  // fetch the host suites and setup proxies
  if(strcmp(action, kOfxActionLoad) == 0) {
      // fetch the host APIs
      OfxStatus stat;
      if((stat = fetchHostSuites(nth)) != kOfxStatOK)
          return stat;
      if((stat = fetchHostDescription(nth)) != kOfxStatOK)
          return stat;
      printHostDescription(nth);

      // see if the host supports overlays
      int supportsOverlays;
      gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropSupportsOverlays, 0, &supportsOverlays);
      if(supportsOverlays != 0) {
        OfxImageEffectHandle effect = (OfxImageEffectHandle ) handle;
        // get the property handle for the plugin
        OfxPropertySetHandle effectProps;
        gEffectHost[nth]->getPropertySet(effect, &effectProps);

        // set the property that is the overlay's main entry point for the plugin
        gPropHost[nth]->propGetPointer(effectProps,  kOfxImageEffectPluginPropOverlayInteractV1, 0, (void **) &gPluginsOverlayMain[nth]);
        if (gPluginsOverlayMain[nth] != 0) {
          gPropHost[nth]->propSetPointer(effectProps, kOfxImageEffectPluginPropOverlayInteractV1, 0,  (void *) overlayMainNthFunc(nth));
        }

    }
  }

  std::stringstream ss;
  ss << gPlugins[nth].pluginIdentifier << "." << action;
  std::stringstream ssr;
  OfxStatus st = kOfxStatErrUnknown;
  try {
    // pre-hooks on some actions (e.g. print or modify parameters)
    if(strcmp(action, kOfxActionLoad) == 0) {
      // no inArgs

      ss << "()";
    }
    else if(strcmp(action, kOfxActionUnload) == 0) {
      // no inArgs

      ss << "()";
    }
    else if(strcmp(action, kOfxActionDescribe) == 0) {
      // no inArgs

      ss << "(" << handle << ")";
    }
    else if(strcmp(action, kOfxActionCreateInstance) == 0) {
      // no inArgs

      ss << "(" << handle << ")";
    } 
    else if(strcmp(action, kOfxActionDestroyInstance) == 0) {
      // no inArgs

      ss << "(" << handle << ")";
    } 
    else if(strcmp(action, kOfxActionBeginInstanceChanged) == 0 ||
            strcmp(action, kOfxActionEndInstanceChanged) == 0) {
      // inArgs has the following properties...
      //     kOfxPropChangeReason - what triggered the change, which will be one of...

      // see why it changed
      char *changeReason;
      gPropHost[nth]->propGetString(inArgs, kOfxPropChangeReason, 0, &changeReason);

      ss << "(" << handle << "," << changeReason << ")";
    }
    else if(strcmp(action, kOfxActionInstanceChanged) == 0) {
      // inArgs has the following properties...
      //     kOfxPropType - the type of the thing that changed which will be one of..
      //     kOfxPropName - the name of the thing that was changed in the instance
      //     kOfxPropChangeReason - what triggered the change, which will be one of...
      //     kOfxPropTime - the effect time at which the chang occured (for Image Effect Plugins only)
      //     kOfxImageEffectPropRenderScale - the render scale currently being applied to any image fetched from a clip (for Image Effect Plugins only)

      // fetch the type of the object that changed
      char *typeChanged;
      gPropHost[nth]->propGetString(inArgs, kOfxPropType, 0, &typeChanged);
      // get the name of the thing that changed
      char *objChanged;
      gPropHost[nth]->propGetString(inArgs, kOfxPropName, 0, &objChanged);
      // see why it changed
      char *changeReason;
      gPropHost[nth]->propGetString(inArgs, kOfxPropChangeReason, 0, &changeReason);
      // get the time
      OfxTime time;
      gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);
      // get the render scale
      OfxPointD renderScale;
      gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, &renderScale.x);

      ss << "(" << handle << "," << typeChanged << "," << objChanged << "," << changeReason << "," << time << ",(" << renderScale.x << "," << renderScale.y << "))";
    }  
    else if(strcmp(action, kOfxActionPurgeCaches) == 0) {
      // no inArgs

      ss << "(" << handle << ")";
    }  
    else if(strcmp(action, kOfxActionSyncPrivateData) == 0) {
      // no inArgs

      ss << "(" << handle << ")";
    }  
    else if(strcmp(action, kOfxActionBeginInstanceEdit) == 0 ||
            strcmp(action, kOfxActionEndInstanceEdit) == 0) {
      // no inArgs

      ss << "(" << handle << ")";
    }  
    else if(strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
      // inArgs has the following property...
      //     kOfxImageEffectPropContext the context being described.

      // get the context from the inArgs handle
      char *context;
      gPropHost[nth]->propGetString(inArgs, kOfxImageEffectPropContext, 0, &context);

      ss << "(" << handle << "," << context << ")";
#ifdef OFX_DEBUG_PROXY_CLIPS
      gContexts[(OfxImageEffectHandle)handle] = context;
#endif
    }
    else if(strcmp(action, kOfxImageEffectActionGetRegionOfDefinition) == 0) {
      // inArgs has the following properties...
      //    kOfxPropTime the effect time for which a region of definition is being requested,
      //    kOfxImageEffectPropRenderScale the render scale that should be used in any calculations in this action,

      // get the time
      OfxTime time;
      gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);
      // get the render scale
      OfxPointD renderScale;
      gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, &renderScale.x);

      ss << "(" << handle << "," << time << ",(" << renderScale.x << "," << renderScale.y << "))";
    }  
    else if(strcmp(action, kOfxImageEffectActionGetRegionsOfInterest) == 0) {
      // inArgs has the following properties...
      //     kOfxPropTime the effect time for which a region of definition is being requested,
      //     kOfxImageEffectPropRenderScale the render scale that should be used in any calculations in this action,
      //     kOfxImageEffectPropRegionOfInterest the region to be rendered in the output image, in Canonical Coordinates.

      // get the time
      OfxTime time;
      gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);
      // get the render scale
      OfxPointD renderScale;
      gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, &renderScale.x);
      ss << "(" << handle << "," << time << ",(" << renderScale.x << "," << renderScale.y << "))";
      // get the RoI the effect is interested in from inArgs
      OfxRectD roi;
      gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRegionOfInterest, 4, &roi.x1);

      ss << "(" << handle << "," << time << ",(" << renderScale.x << "," << renderScale.y << "),(" << roi.x1 << "," << roi.y1 << "," << roi.x2 << "," << roi.y2 << "))";
    }  
    else if(strcmp(action, kOfxImageEffectActionGetFramesNeeded) == 0) {
      // inArgs has the following property...
      //     kOfxPropTime the effect time for which we need to calculate the frames needed on input

      // get the time
      OfxTime time;
      gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);

      ss << "(" << handle << "," << time << ")";
    }    
    else if(strcmp(action, kOfxImageEffectActionIsIdentity) == 0) {
      // inArgs has the following properties...
      //     kOfxPropTime - the time at which to test for identity
      //     kOfxImageEffectPropFieldToRender - the field to test for identity
      //     kOfxImageEffectPropRenderWindow - the window (in \ref PixelCoordinates) to test for identity under
      //     kOfxImageEffectPropRenderScale - the scale factor being applied to the images being renderred

      // get the time from the inArgs
      OfxTime time;
      gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);
      // get the field from the inArgs handle
      char *field;
      gPropHost[nth]->propGetString(inArgs, kOfxImageEffectPropFieldToRender, 0, &field);
      // get the render window
      OfxRectI renderWindow;
      gPropHost[nth]->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, &renderWindow.x1);
      // get the render scale
      OfxPointD renderScale;
      gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, &renderScale.x);

      ss << "(" << handle << "," << time << "," << field << ",(" << renderWindow.x1 << "," << renderWindow.y1 << "," << renderWindow.x2 << "," << renderWindow.y2 << "),(" << renderScale.x << "," << renderScale.y << "))";
    }    
    else if(strcmp(action, kOfxImageEffectActionRender) == 0) {
      // inArgs has the following properties...
      //     kOfxPropTime - the time at which to test for identity
      //     kOfxImageEffectPropFieldToRender - the field to test for identity
      //     kOfxImageEffectPropRenderWindow - the window (in \ref PixelCoordinates) to test for identity under
      //     kOfxImageEffectPropRenderScale - the scale factor being applied to the images being renderred
      //     kOfxImageEffectPropSequentialRenderStatus - whether the effect is currently being rendered in strict frame order on a single instance
      //     kOfxImageEffectPropInteractiveRenderStatus - if the render is in response to a user modifying the effect in an interactive session

      // get the time from the inArgs
      OfxTime time;
      gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);
      // get the field from the inArgs handle
      char *field;
      gPropHost[nth]->propGetString(inArgs, kOfxImageEffectPropFieldToRender, 0, &field);
      // get the render window
      OfxRectI renderWindow;
      gPropHost[nth]->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, &renderWindow.x1);
      // get the render scale
      OfxPointD renderScale;
      gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, &renderScale.x);
      // get the sequential render status
      int sequentialrenderstatus;
      gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropSequentialRenderStatus, 0, &sequentialrenderstatus);
      // get the interactive render status
      int interactiverenderstatus;
      gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropInteractiveRenderStatus, 0, &interactiverenderstatus);
     
      ss << "(" << handle << "," << time << "," << field << ",(" << renderWindow.x1 << "," << renderWindow.y1 << "," << renderWindow.x2 << "," << renderWindow.y2 << "),(" << renderScale.x << "," << renderScale.y << ")," << sequentialrenderstatus << "," << interactiverenderstatus << ")";
    }    
    else if(strcmp(action, kOfxImageEffectActionBeginSequenceRender) == 0 ||
            strcmp(action, kOfxImageEffectActionEndSequenceRender) == 0) {
      // inArgs has the following properties...
      //     kOfxImageEffectPropFrameRange - the range of frames (inclusive) that will be renderred,
      //     kOfxImageEffectPropFrameStep - what is the step between frames, generally set to 1 (for full frame renders) or 0.5 (for fielded renders),
      //     kOfxPropIsInteractive - is this a single frame render due to user interaction in a GUI, or a proper full sequence render.
      //     kOfxImageEffectPropRenderScale - the scale factor to apply to images for this call
      //     kOfxImageEffectPropSequentialRenderStatus - whether the effect is currently being rendered in strict frame order on a single instance
      //     kOfxImageEffectPropInteractiveRenderStatus - if the render is in response to a user modifying the effect in an interactive session

      double range[2];
      gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropFrameRange, 2, range);
      double step;
      gPropHost[nth]->propGetDouble(inArgs, kOfxImageEffectPropFrameStep, 0, &step);
      int isinteractive;
      gPropHost[nth]->propGetInt(inArgs, kOfxPropIsInteractive, 0, &isinteractive);
      // get the render scale
      OfxPointD renderScale;
      gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, &renderScale.x);
      // get the sequential render status
      int sequentialrenderstatus;
      gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropSequentialRenderStatus, 0, &sequentialrenderstatus);
      // get the interactive render status
      int interactiverenderstatus;
      gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropInteractiveRenderStatus, 0, &interactiverenderstatus);
     
      ss << "(" << handle << ",[" << range[0] << "," << range[1] << "]," << step << "," << isinteractive << ",(" << renderScale.x << "," << renderScale.y << ")," << sequentialrenderstatus << "," << interactiverenderstatus << ")";
    }
    else if(strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0) {
      // no inArgs

      ss << "(" << handle << ")";
    }  
    else if(strcmp(action, kOfxImageEffectActionGetTimeDomain) == 0) {
      // no inArgs

      ss << "(" << handle << ")";
    }
    else {
      // unknown OFX Action
      ss << "(" << handle << ") [UNKNOWN ACTION]";
    }

    std::cout << "OFX DebugProxy: " << ss.str() << std::endl;

    assert(gPluginsMainEntry[nth]);
    st =  gPluginsMainEntry[nth](action, handle, inArgs, outArgs);

    
    // post-hooks on some actions (e.g. print or modify result)
    // get the outargs
    if(strcmp(action, kOfxActionLoad) == 0) {
      // no outArgs
    }
    else if(strcmp(action, kOfxActionUnload) == 0) {
      // no outArgs
    }
    else if(strcmp(action, kOfxActionDescribe) == 0) {
      // no outArgs
    }
    else if(strcmp(action, kOfxActionCreateInstance) == 0) {
      // no outArgs
    } 
    else if(strcmp(action, kOfxActionDestroyInstance) == 0) {
      // no outArgs
    } 
    else if(strcmp(action, kOfxActionBeginInstanceChanged) == 0 ||
            strcmp(action, kOfxActionEndInstanceChanged) == 0) {
      // no outArgs
    }
    else if(strcmp(action, kOfxActionInstanceChanged) == 0) {
      // no outArgs
    }  
    else if(strcmp(action, kOfxActionPurgeCaches) == 0) {
      // no outArgs
    }  
    else if(strcmp(action, kOfxActionSyncPrivateData) == 0) {
      // no outArgs
    }  
    else if(strcmp(action, kOfxActionBeginInstanceEdit) == 0 ||
            strcmp(action, kOfxActionEndInstanceEdit) == 0) {
      // no outArgs
    }  
    else if(strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
      // no outArgs
    }
    else if(strcmp(action, kOfxImageEffectActionGetRegionOfDefinition) == 0) {
      // outArgs has the following property which the plug-in may set...
      // kOfxImageEffectPropRegionOfDefinitiong, the calculated region of definition, initially set by the host to the default RoD (see below), in Canonical Coordinates.
      if (st == kOfxStatOK) {
        // get the RoD of the effect from outArgs
        OfxRectD rod;
        gPropHost[nth]->propGetDoubleN(outArgs, kOfxImageEffectPropRegionOfInterest, 4, &rod.x1);
        ssr << "((" << rod.x1 << "," << rod.y1 << "," << rod.x2 << "," << rod.y2 << "))";
      }
    }  
    else if(strcmp(action, kOfxImageEffectActionGetRegionsOfInterest) == 0) {
      // outArgs has a set of 4 dimensional double properties, one for each of the input clips to the effect. The properties are each named "OfxImageClipPropRoI_" with the clip name post pended, for example "OfxImageClipPropRoI_Source". These are initialised to the default RoI. 
      if (st == kOfxStatOK) {
#ifdef OFX_DEBUG_PROXY_CLIPS
        ssr << '(';
        const std::list<std::string>& clips = gClips[nth][getContext(nth,(OfxImageEffectHandle)handle)];
        bool first = true;
        for (std::list<std::string>::const_iterator it = clips.begin(); it != clips.end(); ++it) {
          const char* name = it->c_str();
          std::string clipROIPropName = std::string("OfxImageClipPropRoI_") + name;
          OfxRectD roi;
          OfxStatus pst = gPropHost[nth]->propGetDoubleN(outArgs, clipROIPropName.c_str(), 4, &roi.x1);
          if (pst == kOfxStatOK) {
            if (!first) {
              ssr << ',';
            }
            first = false;
            ssr << name << ":(" << roi.x1 << "," << roi.y1 << "," << roi.x2 << "," << roi.y2 << ")";
          }
        }
        ssr << ')';
#else
        ssr << "(N/A)";
#endif
      }
    }  
    else if(strcmp(action, kOfxImageEffectActionGetFramesNeeded) == 0) {
      // outArgs has a set of properties, one for each input clip, named "OfxImageClipPropFrameRange_" with the name of the clip post-pended. For example "OfxImageClipPropFrameRange_Source". All these properties are multi-dimensional doubles, with the dimension is a multiple of two. Each pair of values indicates a continuous range of frames that is needed on the given input. They are all initalised to the default value. 
      if (st == kOfxStatOK) {
#ifdef OFX_DEBUG_PROXY_CLIPS
        ssr << '(';
        const std::list<std::string>& clips = gClips[nth][getContext(nth,(OfxImageEffectHandle)handle)];
        bool firstclip = true;
        for (std::list<std::string>::const_iterator it = clips.begin(); it != clips.end(); ++it) {
          const char* name = it->c_str();
          double range[2];
          std::string clipFrameRangePropName = std::string("OfxImageClipPropFrameRange_") + name;
          int dim;
          OfxStatus pst = gPropHost[nth]->propGetDimension(outArgs, clipFrameRangePropName.c_str(), &dim);
          if (pst == kOfxStatOK) {
            if (!firstclip) {
              ssr << ',';
            }
            firstclip = false;
            bool firstrange = true;
            ssr << name << ":(";
            for (int i = 0; i < dim; i += 2) {
              gPropHost[nth]->propGetDoubleN(outArgs, clipFrameRangePropName.c_str(), i, &range[0]);
              gPropHost[nth]->propGetDoubleN(outArgs, clipFrameRangePropName.c_str(), i+1, &range[1]);
              if (!firstrange) {
                ssr << ',';
              }
              firstrange = false;
              ssr << name << '(' << range[0] << ',' << range[1] << ')';
            }
            ssr << ')';
          }
        }
#else
        ssr << "(N/A)";
#endif
      }
    }
    else if(strcmp(action, kOfxImageEffectActionIsIdentity) == 0) {
      // outArgs has the following properties which the plugin can set...
      //    kOfxPropName this to the name of the clip that should be used if the effect is an identity transform, defaults to the empty string
      //    kOfxPropTime the time to use from the indicated source clip as an identity image (allowing time slips to happen), defaults to the value in kOfxPropTime in inArgs
      if (st == kOfxStatOK) {
        char *name;
        gPropHost[nth]->propGetString(outArgs, kOfxPropName, 0, &name);
        OfxTime time;
        gPropHost[nth]->propGetDouble(outArgs, kOfxPropTime, 0, &time);
        ssr << "(" << name << "," << time << ")";
      }
    }    
    else if(strcmp(action, kOfxImageEffectActionRender) == 0) {
      // no outArgs
    }    
    else if(strcmp(action, kOfxImageEffectActionBeginSequenceRender) == 0 ||
            strcmp(action, kOfxImageEffectActionEndSequenceRender) == 0) {
      // no outArgs
    }
    else if(strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0) {
      // outArgs has the following properties which the plugin can set...

      //  a set of char * X 1 properties, one for each of the input clips currently attached and the output clip, labelled with "OfxImageClipPropComponents_" post pended with the clip's name. This must be set to one of the component types which the host supports and the effect stated it can accept on that input,
      //  a set of char * X 1 properties, one for each of the input clips currently attached and the output clip, labelled with "OfxImageClipPropDepth_" post pended with the clip's name. This must be set to one of the pixel depths both the host and plugin supports,
      //  a set of double X 1 properties, one for each of the input clips currently attached and the output clip, labelled with "OfxImageClipPropPAR_" post pended with the clip's name. This is the pixel aspect ratio of the input and output clips. This must be set to a positive non zero double value,
      //  kOfxImageEffectPropFrameRate , the frame rate of the output clip, this must be set to a positive non zero double value,
      //  kOfxImageClipPropFieldOrder , the fielding of the output clip,
      //  kOfxImageEffectPropPreMultiplication , the premultiplication of the output clip,
      //  kOfxImageClipPropContinuousSamples, whether the output clip can produce different images at non-frame intervals, defaults to false,
      //  kOfxImageEffectFrameVarying, whether the output clip can produces different images at different times, even if all parameters and inputs are constant, defaults to false.
      if (st == kOfxStatOK) {
#ifdef OFX_DEBUG_PROXY_CLIPS
        OfxStatus pst;
        ssr << '(';
        const std::list<std::string>& clips = gClips[nth][getContext(nth,(OfxImageEffectHandle)handle)];
        bool firstclip = true;
        for (std::list<std::string>::const_iterator it = clips.begin(); it != clips.end(); ++it) {
          const char* name = it ->c_str();
          bool firstpref = true;

          char* components;
          std::string clipComponentsPropName = std::string("OfxImageClipPropComponents_") + name;
          pst = gPropHost[nth]->propGetString(outArgs, clipComponentsPropName.c_str(), 0, &components);
          if (pst == kOfxStatOK) {
             if (firstpref && !firstclip) {
              ssr << ',';
            }
            firstclip = false;
            if (firstpref) {
              ssr << name << ":(";
            } else {
              ssr << ',';
            }
            firstpref = false;
            ssr << "components=" << components;
          }

          char *depth;
          std::string clipDepthPropName = std::string("OfxImageClipPropDepth_") + name;
          pst = gPropHost[nth]->propGetString(outArgs, clipDepthPropName.c_str(), 0, &depth);
          if (pst == kOfxStatOK) {
             if (firstpref && !firstclip) {
              ssr << ',';
            }
            firstclip = false;
            if (firstpref) {
              ssr << name << ":(";
            } else {
              ssr << ',';
            }
            firstpref = false;
            ssr << "depth=" << depth;
          }

          double par;
          std::string clipPARPropName = std::string("OfxImageClipPropPAR_") + name;
          pst = gPropHost[nth]->propGetDouble(outArgs, clipPARPropName.c_str(), 0, &par);
          if (pst == kOfxStatOK) {
             if (firstpref && !firstclip) {
              ssr << ',';
            }
            firstclip = false;
            if (firstpref) {
              ssr << name << ":(";
            } else {
              ssr << ',';
            }
            firstpref = false;
            ssr << "PAR=" << par;
          }

          if (!firstpref) {
              ssr << ')';
          }
        }
        ssr << ')';
#else
        ssr << "(N/A)";
#endif
      }
    }
    else if(strcmp(action, kOfxImageEffectActionGetTimeDomain) == 0) {
      // outArgs has the following property
      //  kOfxImageEffectPropFrameRange - the frame range an effect can produce images for
      if (st == kOfxStatOK) {
        double range[2];
        gPropHost[nth]->propGetDoubleN(outArgs, kOfxImageEffectPropFrameRange, 2, range);

        ssr << "([" << range[0] << "," << range[1] << "])";
      }
    }  
  } catch (std::bad_alloc) {
    // catch memory
    std::cout << "OFX DebugProxy Plugin Memory error." << std::endl;
    return kOfxStatErrMemory;
  } catch ( const std::exception& e ) {
    // standard exceptions
    std::cout << "OFX DebugProxy Plugin error: " << e.what() << std::endl;
    return kOfxStatErrUnknown;
  } catch (int err) {
    // ho hum, gone wrong somehow
    std::cout << "OFX DebugProxy Plugin error: " << err << std::endl;
    return err;
  } catch ( ... ) {
    // everything else
    std::cout << "OFX DebugProxy Plugin error" << std::endl;
    return kOfxStatErrUnknown;
  }
  

  if (ssr.str().empty()) {
    std::cout << "OFX DebugProxy: " << ss.str() << "->" << OFX::StatStr(st) << std::endl;
  } else {
    std::cout << "OFX DebugProxy: " << ss.str() << "->" << OFX::StatStr(st) << ": " << ssr.str() << std::endl;
  }

  return st;
}


template<int nth>
static OfxStatus
pluginMainNth(const char *action, const void *handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
  return pluginMain(nth, action, handle, inArgs, outArgs);
}

#define NTHFUNC(nth) \
    case nth: \
      return pluginMainNth<nth>

static OfxPluginEntryPoint*
pluginMainNthFunc(int nth)
{
  switch (nth) {
          NTHFUNC100(0);
          NTHFUNC100(100);
          NTHFUNC100(200);
  }
  std::cout << "OFX DebugProxy: Error: cannot create main entry point for plugin " << nth << std::endl;
  return 0;
}
#undef NTHFUNC

//////////////// fetchSuite proxy

template<int nth>
void* fetchSuiteNth(OfxPropertySetHandle host, const char *suiteName, int suiteVersion)
{
    void* suite = NULL;
    try {
        suite = gHost[nth]->fetchSuite(host, suiteName, suiteVersion);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..fetchSuite(" << suiteName << "," << suiteVersion << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..fetchSuite(" << suiteName << "," << suiteVersion << ")->" << suite << std::endl;
    if (strcmp(suiteName, kOfxImageEffectSuite) == 0 && suiteVersion == 1) {
        assert(nth < gEffectHost.size() && suite == gEffectHost[nth]);
        return &gEffectProxy[nth];
    }
    return suite;
}
/////////////// getPropertySet proxy
template<int nth>
static OfxStatus
getPropertySetNth(OfxImageEffectHandle imageEffect,
              OfxPropertySetHandle *propHandle)
{
    OfxStatus st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->getPropertySet(imageEffect, propHandle);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..getPropertySet(" << imageEffect << ", " << propHandle << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..getPropertySet(" << imageEffect << ")->" << OFX::StatStr(st) << ": " << *propHandle << std::endl;
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return getPropertySetNth<nth>

static OfxImageEffectSuiteV1getPropertySet
getPropertySetNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create getPropertySet for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC
/////////////// getParamSet proxy

template<int nth>
static OfxStatus
getParamSetNth(OfxImageEffectHandle imageEffect,
              OfxParamSetHandle *paramSet)
{
    OfxStatus st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->getParamSet(imageEffect, paramSet);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..getParamSet(" << imageEffect << ", " << paramSet << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..getParamSet(" << imageEffect << ")->" << OFX::StatStr(st) << ": " << *paramSet << std::endl;
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return getParamSetNth<nth>

static OfxImageEffectSuiteV1getParamSet
getParamSetNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create getParamSet for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// clipDefine proxy
template<int nth>
static OfxStatus
clipDefineNth(OfxImageEffectHandle imageEffect,
              const char *name,
              OfxPropertySetHandle *propertySet)
{
    OfxStatus st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->clipDefine(imageEffect, name, propertySet);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipDefine(" << imageEffect << ", " << name << ", " << propertySet << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipDefine(" << imageEffect << ", " << name << ")->" << OFX::StatStr(st) << ": " << *propertySet << std::endl;
#ifdef OFX_DEBUG_PROXY_CLIPS
    assert(!gContexts[imageEffect].empty());
    gClips[nth][gContexts[imageEffect]].push_back(name);
#endif
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return clipDefineNth<nth>

static OfxImageEffectSuiteV1clipDefine
clipDefineNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create clipDefine for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// clipGetHandle proxy
template<int nth>
static OfxStatus
clipGetHandleNth(OfxImageEffectHandle imageEffect,
                 const char *name,
			     OfxImageClipHandle *clip,
			     OfxPropertySetHandle *propertySet)
{
    OfxStatus st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->clipGetHandle(imageEffect, name, clip, propertySet);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetHandle(" << imageEffect << ", " << name << ", " << clip << ", " << propertySet << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetHandle(" << imageEffect << ", " << name << ")->" << OFX::StatStr(st) << ": (" << *clip << ", " << *propertySet << ")" << std::endl;
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return clipGetHandleNth<nth>

static OfxImageEffectSuiteV1clipGetHandle
clipGetHandleNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create clipGetHandle for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// clipGetPropertySet proxy
template<int nth>
static OfxStatus
clipGetPropertySetNth(OfxImageClipHandle clip,
                      OfxPropertySetHandle *propHandle)
{
    OfxStatus st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->clipGetPropertySet(clip, propHandle);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetPropertySet(" << clip << ", " << propHandle << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetPropertySet(" << clip << ")->" << OFX::StatStr(st) << ": " << *propHandle << std::endl;
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return clipGetPropertySetNth<nth>

static OfxImageEffectSuiteV1clipGetPropertySet
clipGetPropertySetNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create clipGetPropertySet for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// clipGetImage proxy
template<int nth>
static OfxStatus
clipGetImageNth(OfxImageClipHandle clip,
			    OfxTime       time,
			    OfxRectD     *region,
			    OfxPropertySetHandle   *imageHandle)
{
    OfxStatus st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->clipGetImage(clip, time, region, imageHandle);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetImage(" << clip << ", " << time << ", " << region << ", " << imageHandle << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetImage(" << clip << ", " << time << ")->" << OFX::StatStr(st) << ": (";
    if (region) {
        std::cout << "(" << region->x1 << "," << region->y1 << "," << region->x2 << "," << region->y2 << "), ";
    }
    std::cout << *imageHandle << ")" << std::endl;
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return clipGetImageNth<nth>

static OfxImageEffectSuiteV1clipGetImage
clipGetImageNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create clipGetImage for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// clipReleaseImage proxy
template<int nth>
static OfxStatus
clipReleaseImageNth(OfxPropertySetHandle imageHandle)
{
    OfxStatus st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->clipReleaseImage(imageHandle);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipReleaseImage(" << imageHandle << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipReleaseImage(" << imageHandle << ")->" << OFX::StatStr(st) << std::endl;
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return clipReleaseImageNth<nth>

static OfxImageEffectSuiteV1clipReleaseImage
clipReleaseImageNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create clipReleaseImage for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// clipGetRegionOfDefinition proxy
template<int nth>
static OfxStatus
clipGetRegionOfDefinitionNth(OfxImageClipHandle clip,
                             OfxTime time,
                             OfxRectD *bounds)
{
    OfxStatus st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->clipGetRegionOfDefinition(clip, time, bounds);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetRegionOfDefinition(" << clip << ", " << time << ", " << bounds << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetRegionOfDefinition(" << clip << ", " << time << ")->" << OFX::StatStr(st);
    if (bounds) {
        std::cout << ": (" << bounds->x1 << "," << bounds->y1 << "," << bounds->x2 << "," << bounds->y2 << ")";
    }
    std::cout << std::endl;
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return clipGetRegionOfDefinitionNth<nth>

static OfxImageEffectSuiteV1clipGetRegionOfDefinition
clipGetRegionOfDefinitionNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create clipGetRegionOfDefinition for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// abort proxy
template<int nth>
static int
abortNth(OfxImageEffectHandle imageEffect)
{
    int st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->abort(imageEffect);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..abort(" << imageEffect << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..abort(" << imageEffect << ")->" << st << std::endl;
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return abortNth<nth>

static OfxImageEffectSuiteV1abort
abortNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create abort for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// imageMemoryAlloc proxy
template<int nth>
static OfxStatus
imageMemoryAllocNth(OfxImageEffectHandle instanceHandle,
                    size_t nBytes,
                    OfxImageMemoryHandle *memoryHandle)
{
    OfxStatus st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->imageMemoryAlloc(instanceHandle, nBytes, memoryHandle);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryAlloc(" << instanceHandle << ", " << nBytes << ", " << memoryHandle << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryAlloc(" << instanceHandle << ", " << nBytes << ")->" << OFX::StatStr(st) << ": " << *memoryHandle << std::endl;
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return imageMemoryAllocNth<nth>

static OfxImageEffectSuiteV1imageMemoryAlloc
imageMemoryAllocNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create imageMemoryAlloc for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// imageMemoryFree proxy
template<int nth>
static OfxStatus
imageMemoryFreeNth(OfxImageMemoryHandle memoryHandle)
{
    OfxStatus st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->imageMemoryFree(memoryHandle);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryFree(" << memoryHandle << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryFree(" << memoryHandle << ")->" << OFX::StatStr(st) << std::endl;
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return imageMemoryFreeNth<nth>

static OfxImageEffectSuiteV1imageMemoryFree
imageMemoryFreeNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create imageMemoryFree for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// imageMemoryLock proxy
template<int nth>
static OfxStatus
imageMemoryLockNth(OfxImageMemoryHandle memoryHandle,
			       void **returnedPtr)
{
    OfxStatus st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->imageMemoryLock(memoryHandle, returnedPtr);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryLock(" << memoryHandle << ", " << returnedPtr << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryLock(" << memoryHandle << ")->" << OFX::StatStr(st) << ": " << *returnedPtr << std::endl;
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return imageMemoryLockNth<nth>

static OfxImageEffectSuiteV1imageMemoryLock
imageMemoryLockNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create imageMemoryLock for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// imageMemoryUnlock proxy
template<int nth>
static OfxStatus
imageMemoryUnlockNth(OfxImageMemoryHandle memoryHandle)
{
    OfxStatus st;
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    try {
        st = gEffectHost[nth]->imageMemoryUnlock(memoryHandle);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryUnlock(" << memoryHandle << "): host exception!" << std::endl;
        throw;
    }
    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryUnlock(" << memoryHandle << ")->" << OFX::StatStr(st) << std::endl;
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return imageMemoryUnlockNth<nth>

static OfxImageEffectSuiteV1imageMemoryUnlock
imageMemoryUnlockNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create imageMemoryUnlock for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

//////////////// setHost proxy

// function to set the host structure
template<int nth>
static void
setHostNth(OfxHost *hostStruct)
{
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    gHost[nth] = hostStruct;
    gProxy[nth] = *(gHost[nth]);
    gProxy[nth].fetchSuite = fetchSuiteNth<nth>;
    assert(gPluginsSetHost[nth]);
    gPluginsSetHost[nth](&gProxy[nth]);
}

#define NTHFUNC(nth) \
case nth: \
return setHostNth<nth>

static OfxSetHost*
setHostNthFunc(int nth)
{
    switch (nth) {
            NTHFUNC100(0);
            NTHFUNC100(100);
            NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create setHost for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC


// the two mandated functions
OfxPlugin *
OfxGetPlugin(int nth)
{
  // get the OfxPlugin* from the underlying plugin
  OfxPlugin* plugin = OfxGetPlugin_binary ? OfxGetPlugin_binary(nth) : 0;

  if (plugin == 0) {
    std::cout << "OFX DebugProxy: Error: plugin " << nth << " is NULL" << std::endl;
    return plugin;
  }

     
  // copy it
  if (nth+1 > gPlugins.size()) {
    gPlugins.resize(nth+1);
    gPluginsMainEntry.resize(nth+1);
    gPluginsOverlayMain.resize(nth+1);
    gPluginsSetHost.resize(nth+1);
    gHost.resize(nth+1);
    gProxy.resize(nth+1);
  }

  gPlugins[nth].pluginApi = plugin->pluginApi;
  gPlugins[nth].apiVersion = plugin->apiVersion;
  gPlugins[nth].pluginIdentifier = plugin->pluginIdentifier;
  gPlugins[nth].pluginVersionMajor = plugin->pluginVersionMajor;
  gPlugins[nth].pluginVersionMinor = plugin->pluginVersionMinor;
  gPluginsSetHost[nth] = plugin->setHost;
  gPlugins[nth].setHost = setHostNthFunc(nth);
  // modify the main entry point
  gPluginsMainEntry[nth] = plugin->mainEntry;;
  gPlugins[nth].mainEntry = pluginMainNthFunc(nth);
  gPluginsOverlayMain[nth] = 0;

  std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << ".OfxGetPlugin(" << nth << ") -> " << plugin << ", v"
            << plugin->pluginVersionMajor << "." << plugin->pluginVersionMinor << std::endl;

  return &gPlugins[nth];
}
 
int
OfxGetNumberOfPlugins(void)
{
  if (OfxGetNumberOfPlugins_binary == 0) {
     std::cout << "OFX DebugProxy: cannot load plugin from " << gBinaryPath << std::endl;
     gPluginsNb = 0;
  } else {
    gPluginsNb = (*OfxGetNumberOfPlugins_binary)();
    std::cout << "OFX DebugProxy: found " << gPluginsNb << " plugins in " << gBinaryPath << std::endl;
    assert(OfxGetPlugin_binary);
    gPlugins.reserve(gPluginsNb);
    gHost.reserve(gPluginsNb);
    gProxy.reserve(gPluginsNb);
    gEffectHost.reserve(gPluginsNb);
    gPropHost.reserve(gPluginsNb);
    gParamHost.reserve(gPluginsNb);
    gMemoryHost.reserve(gPluginsNb);
    gThreadHost.reserve(gPluginsNb);
    gMessageHost.reserve(gPluginsNb);
    gMessageV2Host.reserve(gPluginsNb);
    gProgressHost.reserve(gPluginsNb);
    gTimeLineHost.reserve(gPluginsNb);
    gParametricParameterHost.reserve(gPluginsNb);
#ifdef OFX_EXTENSIONS_NUKE
    gCameraHost.reserve(gPluginsNb);
    gImageEffectPlaneHost.reserve(gPluginsNb);
    gImageEffectPlaneV2Host.reserve(gPluginsNb);
#endif
#ifdef OX_EXTENSIONS_VEGAS
    gVegasProgressHost.reserve(gPluginsNb);
    gVegasStereoscopicImageHost.reserve(gPluginsNb);
    gVegasKeyframeHost.reserve(gPluginsNb);
#endif

    gInteractHost.reserve(gPluginsNb);
  }

  return gPluginsNb;
}


