/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2016 INRIA
 *
 * openfx-misc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX DebugProxy plugin.
 * Intercept and debug communication between an OFX host and an OFX plugin.
 */

// the clip support code may be wrong. in case of crash, it can be easily disactivated
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
#include "ofxOpenGLRender.h"
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
#include "ofxsMacros.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#if defined __APPLE__ || defined linux || defined __FreeBSD__
#  define EXPORT __attribute__( ( visibility("default") ) )
#elif defined _WIN32
#  define EXPORT OfxExport
#else
#  error Not building on your operating system quite yet
#endif

// the Plugin path can be set here in the source code, or at runtime via the OFX_DEBUGPROXY_BINARY environment variable
#ifndef BINARY_PATH
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#define OFX_PATH "C:\\Program Files\\Common Files\\OFX\\Plugins\\"
#define BINARY_PATH "C:\\Program Files\\Common Files\\OFX\\Plugins.disabled\\Sapphire.ofx.bundle\\Contents\\Win64\\Sapphire.ofx"
#endif
#if defined(__linux__)
#define OFX_PATH "/usr/OFX/Plugins/"
#define BINARY_PATH "/usr/OFX/Plugins.disabled/Sapphire.ofx.bundle/Contents/Linux-x86-64/Sapphire.ofx"
#endif
#if defined(__FreeBSD__)
#define OFX_PATH "/usr/OFX/Plugins/"
#define BINARY_PATH "/usr/OFX/Plugins.disabled/Sapphire.ofx.bundle/Contents/FreeBSD-x86-64/Sapphire.ofx"
#endif
#if defined(__APPLE__)
#define OFX_PATH "/Library/OFX/Plugins/"
#define BINARY_PATH "/Library/OFX/Plugins.disabled.tmp/DEFlicker/DEFlickerAutoLevels.ofx.bundle/Contents/MacOS/DEFlickerAutoLevels.ofx"
#endif
#endif

typedef void (OfxSetHost)(OfxHost *);
typedef OfxStatus (*OfxImageEffectSuiteV1getPropertySet)(OfxImageEffectHandle imageEffect, OfxPropertySetHandle *propHandle);
typedef OfxStatus (*OfxImageEffectSuiteV1getParamSet)(OfxImageEffectHandle imageEffect, OfxParamSetHandle *paramSet);
typedef OfxStatus (*OfxImageEffectSuiteV1clipDefine)(OfxImageEffectHandle imageEffect, const char *name, OfxPropertySetHandle *propertySet);
typedef OfxStatus (*OfxImageEffectSuiteV1clipGetHandle)(OfxImageEffectHandle imageEffect, const char *name, OfxImageClipHandle *clip, OfxPropertySetHandle *propertySet);
typedef OfxStatus (*OfxImageEffectSuiteV1clipGetPropertySet)(OfxImageClipHandle clip, OfxPropertySetHandle *propHandle);
typedef OfxStatus (*OfxImageEffectSuiteV1clipGetImage)(OfxImageClipHandle clip, OfxTime time, const OfxRectD *region, OfxPropertySetHandle *imageHandle);
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


#ifdef OFX_EXTENSIONS_NUKE
//FnOfxImageEffectPlaneSuiteV1
typedef OfxStatus (*FnOfxImageEffectPlaneSuiteV1clipGetImagePlane)(OfxImageClipHandle clip,
                                                                   OfxTime time,
                                                                   const char   *plane,
                                                                   const OfxRectD *region,
                                                                   OfxPropertySetHandle   *imageHandle);

static FnOfxImageEffectPlaneSuiteV1clipGetImagePlane clipGetImagePlaneV1NthFunc(int nth);

//FnOfxImageEffectPlaneSuiteV2
typedef OfxStatus (*FnOfxImageEffectPlaneSuiteV2clipGetImagePlane)(OfxImageClipHandle clip,
                                                                   OfxTime time,
                                                                   int view,
                                                                   const char   *plane,
                                                                   const OfxRectD *region,
                                                                   OfxPropertySetHandle   *imageHandle);
typedef OfxStatus (*FnOfxImageEffectPlaneSuiteV2clipGetRegionOfDefinition)(OfxImageClipHandle clip,
                                                                           OfxTime time,
                                                                           int view,
                                                                           OfxRectD           *bounds);
typedef OfxStatus (*FnOfxImageEffectPlaneSuiteV2getViewName)(OfxImageEffectHandle effect,
                                                             int view,
                                                             const char         **viewName);
typedef OfxStatus (*FnOfxImageEffectPlaneSuiteV2getViewCount)(OfxImageEffectHandle effect,
                                                              int                 *nViews);

static FnOfxImageEffectPlaneSuiteV2clipGetImagePlane clipGetImagePlaneNthFunc(int nth);
static FnOfxImageEffectPlaneSuiteV2clipGetRegionOfDefinition fnClipGetRegionOfDefinitionNthFunc(int nth);
static FnOfxImageEffectPlaneSuiteV2getViewName getViewNameNthFunc(int nth);
static FnOfxImageEffectPlaneSuiteV2getViewCount getViewCountNthFunc(int nth);
#endif

/** @brief A class that lists all the properties of a host */
struct ImageEffectHostDescription
{
public:
    int APIVersionMajor;
    int APIVersionMinor;
    std::string type;
    std::string hostName;
    std::string hostLabel;
    int versionMajor;
    int versionMinor;
    int versionMicro;
    std::string versionLabel;
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
    bool multiPlanar;
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
static std::vector<OfxImageEffectOpenGLRenderSuiteV1*> gOpenGLRenderHost;
#ifdef OFX_EXTENSIONS_NUKE
static std::vector<NukeOfxCameraSuiteV1*>             gCameraHost;
static std::vector<FnOfxImageEffectPlaneSuiteV1*>     gImageEffectPlaneV1Host;
static std::vector<FnOfxImageEffectPlaneSuiteV1>      gImageEffectPlaneV1Proxy;
static std::vector<FnOfxImageEffectPlaneSuiteV2*>     gImageEffectPlaneV2Host;
static std::vector<FnOfxImageEffectPlaneSuiteV2>      gImageEffectPlaneV2Proxy;
#endif
#ifdef OFX_EXTENSIONS_VEGAS
static std::vector<OfxVegasProgressSuiteV1*>          gVegasProgressHost;
static std::vector<OfxVegasStereoscopicImageSuiteV1*> gVegasStereoscopicImageHost;
static std::vector<OfxVegasKeyframeSuiteV1*>          gVegasKeyframeHost;
#endif
// undocumented suite (present in Sony Catalyst Edit)
static std::vector<void*> gOpenCLProgramHost;
#define kOfxOpenCLProgramSuite "OfxOpenCLProgramSuite"


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
static const char* help_string =
    "OFX DebugProxy Help:\n"
    "- Specify the PATH to the plugin to be debugged using the environment variable\n"
    "  OFX_DEBUGPROXY_BINARY.\n"
    "  this can be done on Unix/Linux/FreeBSD/OSX using something like:\n"
    "  env OFX_DEBUGPROXY_BINARY=/path/to/plugindir/plugin.ofx /path/to/ofx/host/bin/host\n"
    "  the first path points to the plugin binary (ending in \".ofx\"), and the second\n"
    "  path is the host executable.\n"
#if defined(__APPLE__)
    "  The OS X executable for Nuke is usually in\n"
    "  /Applications/Nuke<version>/Nuke<version>.app/Contents/MacOS/Nuke<version>\n"
#endif
    "- Note that the plugin must NOT be in a default location for OFX plugins, or\n"
    "  it will be loaded twice.\n"
    "  The default locations for plugins on this system is "OFX_PATH "\n"
    "- If the plugin depends on dynamic libraries and cannot find them, you can\n"
    "  modify the path to locate them by adding the directory containing the\n"
    "  dependency (usually the same as the plugin location) to\n"
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    "  the global %PATH%.\n"
#endif
#if defined(__linux__)
    "  the environment variable LD_LIBRARY_PATH\n"
    "  (add \"LD_LIBRARY_PATH=/path/to/plugindir\" after the \"env\" in the line above).\n"
#endif
#if defined(__FreeBSD__)
    "  the environment variable LD_LIBRARY_PATH\n"
    "  (add \"LD_LIBRARY_PATH=/path/to/plugindir\" after the \"env\" in the line above).\n"
#endif
#if defined(__APPLE__)
    "  the environment variable DYLD_LIBRARY_PATH\n"
    "  (add \"DYLD_LIBRARY_PATH=/path/to/plugindir after the \"env\" in the line above).\n"
#endif
    "- If the value of OFX_DEBUGPROXY_BINARY is changed, or if the plugin is modified\n"
    "  or recompiled, the OFX host may not take this into account, since the \n"
    "  DebugProxy plugin itself is unchanged. You have to either clean up the OFX\n"
    "  Plugin cache in the host, or modify the date of the DebugProxy binary.\n"
#if defined(__linux__)
    "  On Linux, this can be done using the following command:\n"
    "  touch "OFX_PATH "DebugProxy.ofx.bundle/Contents/Linux-x86*/DebugProxy.ofx\n"
#endif
#if defined(__FreeBSD__)
    "  On FreeBSD, this can be done using the following command:\n"
    "  touch "OFX_PATH "DebugProxy.ofx.bundle/Contents/FreeBSD-x86*/DebugProxy.ofx\n"
#endif
#if defined(__APPLE__)
    "  On OS X, this can be done using the following command:\n"
    "  touch "OFX_PATH "DebugProxy.ofx.bundle/Contents/MacOS/DebugProxy.ofx\n"
#endif
;

// load the underlying binary
struct Loader
{
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
            if (std::strncmp( gBinaryPath, OFX_PATH, std::strlen(OFX_PATH) ) == 0) {
                std::cout << "OFX DebugProxy: Error: plugin binary seems to be in the default plugin path:" << std::endl;
                std::cout << "OFX DebugProxy: OFX_DEBUGPROXY_BINARY=" << gBinaryPath << " is in " << OFX_PATH << std::endl;
                std::cout << "OFX DebugProxy: This is probably a very bad idea, since the host will load twice" << std::endl;
                std::cout << "OFX DebugProxy: the same plugin, and wouldn't be able to tell one from the other." << std::endl;
                std::cout << "OFX DebugProxy: Please move the plugin binary to another location.\n" << std::endl;

                return;
            }
            gBinary = new OFX::Binary(gBinaryPath);
        }
        if (gBinary) {
            gBinary->load();
            if ( gBinary->isInvalid() ) {
                std::cout << "OFX DebugProxy: Error: Cannot load the plugin binary." << std::endl;
                std::cout << "OFX DebugProxy: OFX_DEBUGPROXY_BINARY=" << gBinaryPath << " is propably not an OFX plugin," << std::endl;
                std::cout << "OFX DebugProxy:  or misses some of its dynamic dependencies (see help below)." << std::endl;
                std::cout << help_string;

                return;
            }
            // fetch the binary entry points
            OfxGetNumberOfPlugins_binary = ( int (*)() )gBinary->findSymbol("OfxGetNumberOfPlugins");
            OfxGetPlugin_binary = ( OfxPlugin * (*)(int) )gBinary->findSymbol("OfxGetPlugin");
            if (!OfxGetNumberOfPlugins_binary || !OfxGetPlugin_binary) {
                std::cout << "OFX DebugProxy: Error: Cannot find the mandatory symbols OfxGetNumberOfPlugins and OfxGetPlugin." << std::endl;
                std::cout << "OFX DebugProxy: OFX_DEBUGPROXY_BINARY=" << gBinaryPath << " is propably not an OFX plugin." << std::endl;
                std::cout << help_string;
                gBinary->setInvalid(true);

                return;
            }
            std::cout << "OFX DebugProxy: OFX_DEBUGPROXY_BINARY=" << gBinaryPath << " succesfully loaded" << std::endl;
        } else {
            std::cout << "OFX DebugProxy: Error: Cannot load the plugin binary OFX_DEBUGPROXY_BINARY=" << gBinaryPath <<  std::endl;
            std::cout << help_string;

            return;
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
    assert( nth < (int)gHost.size() );
    if (!gHost[nth]) {
        return kOfxStatErrMissingHostFeature;
    }

    if ( nth + 1 > (int)gEffectHost.size() ) {
        gEffectHost.resize(nth + 1);
        gEffectProxy.resize(nth + 1);
        gPropHost.resize(nth + 1);
        gParamHost.resize(nth + 1);
        gMemoryHost.resize(nth + 1);
        gThreadHost.resize(nth + 1);
        gMessageHost.resize(nth + 1);
        gMessageV2Host.resize(nth + 1);
        gProgressHost.resize(nth + 1);
        gTimeLineHost.resize(nth + 1);
        gParametricParameterHost.resize(nth + 1);
        gOpenGLRenderHost.resize(nth + 1);
#ifdef OFX_EXTENSIONS_NUKE
        gCameraHost.resize(nth + 1);
        gImageEffectPlaneV1Host.resize(nth + 1);
        gImageEffectPlaneV1Proxy.resize(nth + 1);
        gImageEffectPlaneV2Host.resize(nth + 1);
        gImageEffectPlaneV2Proxy.resize(nth + 1);
#endif
#ifdef OFX_EXTENSIONS_VEGAS
        gVegasProgressHost.resize(nth + 1);
        gVegasStereoscopicImageHost.resize(nth + 1);
        gVegasKeyframeHost.resize(nth + 1);
#endif
        gOpenCLProgramHost.resize(nth + 1);

        gInteractHost.resize(nth + 1);
#ifdef OFX_DEBUG_PROXY_CLIPS
        gClips.resize(nth + 1);
#endif
    }

    if (!gHost[nth]->fetchSuite) {
        return kOfxStatErrMissingHostFeature;
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
    gOpenGLRenderHost[nth]    = (OfxImageEffectOpenGLRenderSuiteV1 *) gHost[nth]->fetchSuite(gHost[nth]->host, kOfxOpenGLRenderSuite, 1);
#ifdef OFX_EXTENSIONS_NUKE
    gCameraHost[nth]                 = (NukeOfxCameraSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kNukeOfxCameraSuite, 1);
    gImageEffectPlaneV1Host[nth]     = (FnOfxImageEffectPlaneSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kFnOfxImageEffectPlaneSuite, 1);
    if (gImageEffectPlaneV1Host[nth]) {
        gImageEffectPlaneV1Proxy[nth]    = *gImageEffectPlaneV1Host[nth];
        gImageEffectPlaneV1Proxy[nth].clipGetImagePlane = clipGetImagePlaneV1NthFunc(nth);
    } else {
        gImageEffectPlaneV1Proxy[nth].clipGetImagePlane = NULL;
    }
    gImageEffectPlaneV2Host[nth]     = (FnOfxImageEffectPlaneSuiteV2 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kFnOfxImageEffectPlaneSuite, 2);
    if (gImageEffectPlaneV2Host[nth]) {
        gImageEffectPlaneV2Proxy[nth]    = *gImageEffectPlaneV2Host[nth];
        gImageEffectPlaneV2Proxy[nth].clipGetImagePlane = clipGetImagePlaneNthFunc(nth);
        gImageEffectPlaneV2Proxy[nth].clipGetRegionOfDefinition = fnClipGetRegionOfDefinitionNthFunc(nth);
        gImageEffectPlaneV2Proxy[nth].getViewName = getViewNameNthFunc(nth);
        gImageEffectPlaneV2Proxy[nth].getViewCount = getViewCountNthFunc(nth);
    } else {
        gImageEffectPlaneV2Proxy[nth].clipGetImagePlane = NULL;
        gImageEffectPlaneV2Proxy[nth].clipGetRegionOfDefinition = NULL;
        gImageEffectPlaneV2Proxy[nth].getViewName = NULL;
        gImageEffectPlaneV2Proxy[nth].getViewCount = NULL;
    }
#endif
#ifdef OFX_EXTENSIONS_VEGAS
    gVegasProgressHost[nth]          = (OfxVegasProgressSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxVegasProgressSuite, 1);
    gVegasStereoscopicImageHost[nth] = (OfxVegasStereoscopicImageSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxVegasStereoscopicImageEffectSuite, 1);
    gVegasKeyframeHost[nth]          = (OfxVegasKeyframeSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxVegasKeyframeSuite, 1);
#endif
    gOpenCLProgramHost[nth] = (void*)gHost[nth]->fetchSuite(gHost[nth]->host, kOfxOpenCLProgramSuite, 1);
    gInteractHost[nth]   = (OfxInteractSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxInteractSuite, 1);
    if (!gEffectHost[nth] || !gPropHost[nth] || !gParamHost[nth] || !gMemoryHost[nth] || !gThreadHost[nth]) {
        return kOfxStatErrMissingHostFeature;
    }
    // setup proxies
    if (gEffectHost[nth]) {
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
    } else {
        gEffectProxy[nth].getPropertySet = NULL;
        gEffectProxy[nth].getParamSet = NULL;
        gEffectProxy[nth].clipDefine = NULL;
        gEffectProxy[nth].clipGetHandle = NULL;
        gEffectProxy[nth].clipGetPropertySet = NULL;
        gEffectProxy[nth].clipGetImage = NULL;
        gEffectProxy[nth].clipReleaseImage = NULL;
        gEffectProxy[nth].clipGetRegionOfDefinition = NULL;
        gEffectProxy[nth].abort = NULL;
        gEffectProxy[nth].imageMemoryAlloc = NULL;
        gEffectProxy[nth].imageMemoryFree = NULL;
        gEffectProxy[nth].imageMemoryLock = NULL;
        gEffectProxy[nth].imageMemoryUnlock = NULL;
    }

    return kOfxStatOK;
} // fetchHostSuites

inline OfxStatus
fetchHostDescription(int nth)
{
    assert( nth < (int)gHost.size() );
    if (!gHost[nth]) {
        return kOfxStatErrMissingHostFeature;
    }

    if ( nth + 1 > (int)gHostDescription.size() ) {
        gHostDescription.resize(nth + 1);
    }

    OfxPropertySetHandle host = gHost[nth]->host;
    ImageEffectHostDescription &hostDesc = gHostDescription[nth];
    OfxStatus st;
    // and get some properties
    if (!gPropHost[nth] ||
        !gPropHost[nth]->propGetInt ||
        !gPropHost[nth]->propGetString ||
        !gPropHost[nth]->propGetDimension) {
        return kOfxStatErrMissingHostFeature;
    }
    {
        int v = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxPropAPIVersion, 0, &v);
        hostDesc.APIVersionMajor = (st == kOfxStatOK) ? v : 1; // default to 1.0
        st = gPropHost[nth]->propGetInt(host, kOfxPropAPIVersion, 1, &v);
        hostDesc.APIVersionMinor = (st == kOfxStatOK) ? v : 0;
    }
    {
        char *type = 0;
        st = gPropHost[nth]->propGetString(host, kOfxPropType, 0, &type);
        assert(st == kOfxStatOK);
        hostDesc.type = type;
    }
    {
        char *name = 0;
        st = gPropHost[nth]->propGetString(host, kOfxPropName, 0, &name);
        assert(st == kOfxStatOK);
        hostDesc.hostName = name;
    }
    {
        char *s = 0;
        st = gPropHost[nth]->propGetString(host, kOfxPropLabel, 0, &s);
        assert(st == kOfxStatOK);
        hostDesc.hostLabel = s;
    }
    {
        int v = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxPropVersion, 0, &v);
        hostDesc.versionMajor = (st == kOfxStatOK) ? v : 0;
        st = gPropHost[nth]->propGetInt(host, kOfxPropVersion, 1, &v);
        hostDesc.versionMinor = (st == kOfxStatOK) ? v : 0;
        st = gPropHost[nth]->propGetInt(host, kOfxPropVersion, 2, &v);
        hostDesc.versionMicro = (st == kOfxStatOK) ? v : 0;
    }
    {
        char *s = 0;
        st = gPropHost[nth]->propGetString(host, kOfxPropVersionLabel, 0, &s);
        assert(st == kOfxStatOK);
        hostDesc.versionLabel = s;
    }
    {
        int hostIsBackground = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectHostPropIsBackground, 0, &hostIsBackground);
        hostDesc.hostIsBackground = (st == kOfxStatOK) && hostIsBackground != 0;
    }
    {
        int supportsOverlays = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropSupportsOverlays, 0, &supportsOverlays);
        hostDesc.supportsOverlays = (st == kOfxStatOK) && supportsOverlays != 0;
    }
    {
        int supportsMultiResolution = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropSupportsMultiResolution, 0, &supportsMultiResolution);
        hostDesc.supportsMultiResolution = (st == kOfxStatOK) && supportsMultiResolution != 0;
    }
    {
        int supportsTiles = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropSupportsTiles, 0, &supportsTiles);
        hostDesc.supportsTiles = (st == kOfxStatOK) && supportsTiles != 0;
    }
    {
        int temporalClipAccess = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropTemporalClipAccess, 0, &temporalClipAccess);
        hostDesc.temporalClipAccess = (st == kOfxStatOK) && temporalClipAccess != 0;
    }
    int numComponents = 0;
    st = gPropHost[nth]->propGetDimension(host, kOfxImageEffectPropSupportedComponents, &numComponents);
    assert (st == kOfxStatOK);
    for (int i = 0; i < numComponents; ++i) {
        char *comp = 0;
        st = gPropHost[nth]->propGetString(host, kOfxImageEffectPropSupportedComponents, i, &comp);
        assert (st == kOfxStatOK);
        hostDesc._supportedComponents.push_back(comp);
    }
    int numContexts = 0;
    st = gPropHost[nth]->propGetDimension(host, kOfxImageEffectPropSupportedContexts, &numContexts);
    assert (st == kOfxStatOK);
    for (int i = 0; i < numContexts; ++i) {
        char* cont = 0;
        st = gPropHost[nth]->propGetString(host, kOfxImageEffectPropSupportedContexts, i, &cont);
        assert (st == kOfxStatOK);
        hostDesc._supportedContexts.push_back(cont);
    }
    int numPixelDepths = 0;
    st = gPropHost[nth]->propGetDimension(host, kOfxImageEffectPropSupportedPixelDepths, &numPixelDepths);
    assert (st == kOfxStatOK);
    for (int i = 0; i < numPixelDepths; ++i) {
        char *depth = 0;
        st = gPropHost[nth]->propGetString(host, kOfxImageEffectPropSupportedPixelDepths, i, &depth);
        assert (st == kOfxStatOK);
        hostDesc._supportedPixelDepths.push_back(depth);
    }
    {
        int supportsMultipleClipDepths = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectHostPropIsBackground, 0, &supportsMultipleClipDepths);
        hostDesc.supportsMultipleClipDepths = (st == kOfxStatOK) && supportsMultipleClipDepths != 0;
    }
    {
        int supportsMultipleClipPARs = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropSupportsMultipleClipPARs, 0, &supportsMultipleClipPARs);
        hostDesc.supportsMultipleClipPARs = (st == kOfxStatOK) && supportsMultipleClipPARs != 0;
    }
    {
        int supportsSetableFrameRate = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropSetableFrameRate, 0, &supportsSetableFrameRate);
        hostDesc.supportsSetableFrameRate = (st == kOfxStatOK) && supportsSetableFrameRate != 0;
    }
    {
        int supportsSetableFielding = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxImageEffectPropSetableFielding, 0, &supportsSetableFielding);
        hostDesc.supportsSetableFielding = (st == kOfxStatOK) && supportsSetableFielding != 0;
    }
    {
        int supportsStringAnimation = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropSupportsStringAnimation, 0, &supportsStringAnimation);
        hostDesc.supportsStringAnimation = (st == kOfxStatOK) && supportsStringAnimation != 0;
    }
    {
        int supportsCustomInteract = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropSupportsCustomInteract, 0, &supportsCustomInteract);
        hostDesc.supportsCustomInteract = (st == kOfxStatOK) && supportsCustomInteract != 0;
    }
    {
        int supportsChoiceAnimation = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropSupportsChoiceAnimation, 0, &supportsChoiceAnimation);
        hostDesc.supportsChoiceAnimation = (st == kOfxStatOK) && supportsChoiceAnimation != 0;
    }
    {
        int supportsBooleanAnimation = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropSupportsBooleanAnimation, 0, &supportsBooleanAnimation);
        hostDesc.supportsBooleanAnimation = (st == kOfxStatOK) && supportsBooleanAnimation != 0;
    }
    {
        int supportsCustomAnimation = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropSupportsCustomAnimation, 0, &supportsCustomAnimation);
        hostDesc.supportsCustomAnimation = (st == kOfxStatOK) && supportsCustomAnimation != 0;
    }
    {
        int supportsParametricAnimation = 0;
        st = gPropHost[nth]->propGetInt(host, kOfxParamHostPropSupportsParametricAnimation, 0, &supportsParametricAnimation);
        hostDesc.supportsParametricAnimation = (st == kOfxStatOK) && supportsParametricAnimation != 0;
    }
#ifdef OFX_EXTENSIONS_NUKE
    {
        int canTransform = 0;
        st = gPropHost[nth]->propGetInt(host, kFnOfxImageEffectCanTransform, 0, &canTransform);
        hostDesc.canTransform = (st == kOfxStatOK) && canTransform != 0;
    }
    {
        int multiplanar = 0;
        st = gPropHost[nth]->propGetInt(host, kFnOfxImageEffectPropMultiPlanar, 0, &multiplanar);
        hostDesc.multiPlanar = (st == kOfxStatOK) && multiplanar != 0;
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
} // fetchHostDescription

inline void
printHostDescription(int nth)
{
    const ImageEffectHostDescription &hostDesc = gHostDescription[nth];
    std::cout << "OFX DebugProxy: host description follows" << std::endl;
    std::cout << "OFX API version " << hostDesc.APIVersionMajor << '.' << hostDesc.APIVersionMinor << std::endl;
    std::cout << "type=" << hostDesc.type << std::endl;
    std::cout << "hostName=" << hostDesc.hostName << std::endl;
    std::cout << "hostLabel=" << hostDesc.hostLabel << std::endl;
    std::cout << "hostVersion=" << hostDesc.versionMajor << '.' << hostDesc.versionMinor << '.' << hostDesc.versionMicro;
    std::cout << " (" << hostDesc.versionLabel << ')' << std::endl;
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
    if (gThreadHost[nth]) {
        std::cout << kOfxMultiThreadSuite << ',';
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
    if (gOpenGLRenderHost[nth]) {
        std::cout << kOfxOpenGLRenderSuite << ',';
    }
#ifdef OFX_EXTENSIONS_NUKE
    if (gCameraHost[nth]) {
        std::cout << kNukeOfxCameraSuite << ',';
    }
    if (gImageEffectPlaneV1Host[nth]) {
        std::cout << kFnOfxImageEffectPlaneSuite << "V1" << ',';
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
    if (gOpenCLProgramHost[nth]) {
        std::cout << kOfxOpenCLProgramSuite << ',';
    }
    std::cout << std::endl;
    std::cout << "OFX DebugProxy: host description finished" << std::endl;
} // printHostDescription

////////////////////////////////////////////////////////////////////////////////
// the entry point for the overlay
static OfxStatus
overlayMain(int nth,
            const char *action,
            const void *handle,
            OfxPropertySetHandle inArgs,
            OfxPropertySetHandle outArgs)
{
    std::stringstream ss;

    ss << gPlugins[nth].pluginIdentifier << ".i." << action;
    std::stringstream ssr;
    OfxStatus st = kOfxStatErrUnknown;
    try {
        // pre-hooks on some actions (e.g. print or modify parameters)
        if (strcmp(action, kOfxActionDescribe) == 0) {
            // no inArgs

            ss << "(" << handle << ")";
        } else if (strcmp(action, kOfxActionCreateInstance) == 0) {
            // no inArgs

            ss << "(" << handle << ")";
        } else if (strcmp(action, kOfxActionDestroyInstance) == 0) {
            // no inArgs

            ss << "(" << handle << ")";
        } else if (strcmp(action, kOfxInteractActionDraw) == 0) {
            // inArgs has the following properties on an image effect plugin,
            //     kOfxPropEffectInstance - a handle to the effect for which the interact has been,
            //     kOfxInteractPropViewportSize - the openGL viewport size for the instance
            //     kOfxInteractPropPixelScale - the scale factor to convert cannonical pixels to screen pixels
            //     kOfxInteractPropBackgroundColour - the background colour of the application behind the current view
            //     kOfxPropTime - the effect time at which changed occured
            //     kOfxImageEffectPropRenderScale - the render scale applied to any image fetched

            ss << "(" << handle << ",TODO)";
        } else if ( ( strcmp(action, kOfxInteractActionPenMotion) == 0) ||
                    ( strcmp(action, kOfxInteractActionPenDown) == 0) ||
                    ( strcmp(action, kOfxInteractActionPenUp) == 0) ) {
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
        } else if ( ( strcmp(action, kOfxInteractActionKeyDown) == 0) ||
                    ( strcmp(action, kOfxInteractActionKeyUp) == 0) ||
                    ( strcmp(action, kOfxInteractActionKeyRepeat) == 0) ) {
            // inArgs has the following properties on an image effect plugin,
            //     kOfxPropEffectInstance - a handle to the effect for which the interact has been,
            //     kOfxPropKeySym - single integer value representing the key that was manipulated, this may not have a UTF8 representation (eg: a return key)
            //     kOfxPropKeyString - UTF8 string representing a character key that was pressed, some keys have no UTF8 encoding, in which case this is ""
            //     kOfxPropTime - the effect time at which changed occured
            //     kOfxImageEffectPropRenderScale - the render scale applied to any image fetched

            ss << "(" << handle << ",TODO)";
        } else if ( ( strcmp(action, kOfxInteractActionGainFocus) == 0) ||
                    ( strcmp(action, kOfxInteractActionLoseFocus) == 0) ) {
            // inArgs has the following properties on an image effect plugin,
            //     kOfxPropEffectInstance - a handle to the effect for which the interact is being used on,
            //     kOfxInteractPropViewportSize - the openGL viewport size for the instance,
            //     kOfxInteractPropPixelScale - the scale factor to convert cannonical pixels to screen pixels,
            //     kOfxInteractPropBackgroundColour - the background colour of the application behind the current view
            //     kOfxPropTime - the effect time at which changed occured
            //     kOfxImageEffectPropRenderScale - the render scale applied to any image fetched

            ss << "(" << handle << ",TODO)";
        } else {
            // unknown OFX Action
            ss << "(" << handle << ") [UNKNOWN ACTION]";
        }


        std::cout << "OFX DebugProxy: " << ss.str() << std::endl;

        st =  gPluginsOverlayMain[nth](action, handle, inArgs, outArgs);

        // post-hooks on some actions (e.g. print or modify result)
        // get the outargs
        if (strcmp(action, kOfxActionDescribe) == 0) {
            // no outArgs
        } else if (strcmp(action, kOfxActionCreateInstance) == 0) {
            // no outArgs
        } else if (strcmp(action, kOfxActionDestroyInstance) == 0) {
            // no outArgs
        } else if (strcmp(action, kOfxInteractActionDraw) == 0) {
            // no outArgs
        } else if ( ( strcmp(action, kOfxInteractActionPenMotion) == 0) ||
                    ( strcmp(action, kOfxInteractActionPenDown) == 0) ||
                    ( strcmp(action, kOfxInteractActionPenUp) == 0) ) {
            // no outArgs
        } else if ( ( strcmp(action, kOfxInteractActionKeyDown) == 0) ||
                    ( strcmp(action, kOfxInteractActionKeyUp) == 0) ||
                    ( strcmp(action, kOfxInteractActionKeyRepeat) == 0) ) {
            // no outArgs
        } else if ( ( strcmp(action, kOfxInteractActionGainFocus) == 0) ||
                    ( strcmp(action, kOfxInteractActionLoseFocus) == 0) ) {
            // no outArgs
        }
    } catch (std::bad_alloc) {
        // catch memory
        std::cout << "OFX DebugProxy Plugin Memory error." << std::endl;

        return kOfxStatErrMemory;
    } catch (const std::exception& e) {
        // standard exceptions
        std::cout << "OFX DebugProxy Plugin error: " << e.what() << std::endl;

        return kOfxStatErrUnknown;
    } catch (int err) {
        // ho hum, gone wrong somehow
        std::cout << "OFX DebugProxy Plugin error: " << err << std::endl;

        return err;
    } catch (...) {
        // everything else
        std::cout << "OFX DebugProxy Plugin error" << std::endl;

        return kOfxStatErrUnknown;
    }

    if ( ssr.str().empty() ) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << ".i." << action << "->" << OFX::StatStr(st) << std::endl;
    } else {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << ".i." << action << "->" << OFX::StatStr(st) << ": " << ssr.str() << std::endl;
    }

    return st;
} // overlayMain

template<int nth>
static OfxStatus
overlayMainNth(const char *action,
               const void *handle,
               OfxPropertySetHandle inArgs,
               OfxPropertySetHandle outArgs)
{
    return overlayMain(nth, action, handle, inArgs, outArgs);
}

#define NTHFUNC10(nth) \
    NTHFUNC(nth); \
    NTHFUNC(nth + 1); \
    NTHFUNC(nth + 2); \
    NTHFUNC(nth + 3); \
    NTHFUNC(nth + 4); \
    NTHFUNC(nth + 5); \
    NTHFUNC(nth + 6); \
    NTHFUNC(nth + 7); \
    NTHFUNC(nth + 8); \
    NTHFUNC(nth + 9)

#define NTHFUNC100(nth) \
    NTHFUNC10(nth); \
    NTHFUNC10(nth + 10); \
    NTHFUNC10(nth + 20); \
    NTHFUNC10(nth + 30); \
    NTHFUNC10(nth + 40); \
    NTHFUNC10(nth + 50); \
    NTHFUNC10(nth + 60); \
    NTHFUNC10(nth + 70); \
    NTHFUNC10(nth + 80); \
    NTHFUNC10(nth + 90)

#define NTHFUNC(nth) \
case nth: \
    return overlayMainNth < nth >

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
static std::string
getContext(int nth,
           OfxImageEffectHandle handle)
{
    if (!gEffectHost[nth] ||
        !gEffectHost[nth]->getPropertySet ||
        !gPropHost[nth] ||
        !gPropHost[nth]->propGetString) {
        return "ERROR";
    }
    // fetch effect props
    OfxPropertySetHandle propHandle;
    OfxStatus st = gEffectHost[nth]->getPropertySet( (OfxImageEffectHandle)handle, &propHandle );

    assert(st == kOfxStatOK);
    // get context
    char *context = 0;
    st = gPropHost[nth]->propGetString(propHandle, kOfxImageEffectPropContext, 0, &context);
    assert(st == kOfxStatOK);

    return context;
}

#endif

////////////////////////////////////////////////////////////////////////////////
// The main function
static OfxStatus
pluginMain(int nth,
           const char *action,
           const void *handle,
           OfxPropertySetHandle inArgs,
           OfxPropertySetHandle outArgs)
{
    // fetch the host suites and setup proxies
    if (strcmp(action, kOfxActionLoad) == 0) {
        // fetch the host APIs
        OfxStatus stat;
        if ( ( stat = fetchHostSuites(nth) ) != kOfxStatOK ) {
            return stat;
        }
        if ( ( stat = fetchHostDescription(nth) ) != kOfxStatOK ) {
            return stat;
        }
        printHostDescription(nth);
    }

    std::stringstream ss;
    ss << gPlugins[nth].pluginIdentifier << "." << action;
    std::stringstream ssr;
    OfxStatus st = kOfxStatErrUnknown;
    try {
        // pre-hooks on some actions (e.g. print or modify parameters)
        if (strcmp(action, kOfxActionLoad) == 0) {
            // no inArgs

            ss << "()";
        } else if (strcmp(action, kOfxActionUnload) == 0) {
            // no inArgs

            ss << "()";
        } else if (strcmp(action, kOfxActionDescribe) == 0) {
            // no inArgs

            ss << "(" << handle << ")";
        } else if (strcmp(action, kOfxActionCreateInstance) == 0) {
            // no inArgs

            ss << "(" << handle << ")";
        } else if (strcmp(action, kOfxActionDestroyInstance) == 0) {
            // no inArgs

            ss << "(" << handle << ")";
        } else if ( ( strcmp(action, kOfxActionBeginInstanceChanged) == 0) ||
                    ( strcmp(action, kOfxActionEndInstanceChanged) == 0) ) {
            // inArgs has the following properties...
            //     kOfxPropChangeReason - what triggered the change, which will be one of...

            // see why it changed
            char *changeReason = 0;
            gPropHost[nth]->propGetString(inArgs, kOfxPropChangeReason, 0, &changeReason);

            ss << "(" << handle << "," << changeReason << ")";
        } else if (strcmp(action, kOfxActionInstanceChanged) == 0) {
            // inArgs has the following properties...
            //     kOfxPropType - the type of the thing that changed which will be one of..
            //     kOfxPropName - the name of the thing that was changed in the instance
            //     kOfxPropChangeReason - what triggered the change, which will be one of...
            //     kOfxPropTime - the effect time at which the chang occured (for Image Effect Plugins only)
            //     kOfxImageEffectPropRenderScale - the render scale currently being applied to any image fetched from a clip (for Image Effect Plugins only)

            // fetch the type of the object that changed
            char *typeChanged = 0;
            gPropHost[nth]->propGetString(inArgs, kOfxPropType, 0, &typeChanged);
            // get the name of the thing that changed
            char *objChanged = 0;
            gPropHost[nth]->propGetString(inArgs, kOfxPropName, 0, &objChanged);
            // see why it changed
            char *changeReason = 0;
            gPropHost[nth]->propGetString(inArgs, kOfxPropChangeReason, 0, &changeReason);
            // get the time
            OfxTime time = 0.;
            gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);
            // get the render scale
            double renderScale[2] = {0., 0.};
            gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, renderScale);

            ss << "(" << handle << "," << typeChanged << "," << objChanged << "," << changeReason << "," << time << ",(" << renderScale[0] << "," << renderScale[1] << "))";
        } else if (strcmp(action, kOfxActionPurgeCaches) == 0) {
            // no inArgs

            ss << "(" << handle << ")";
        } else if (strcmp(action, kOfxActionSyncPrivateData) == 0) {
            // no inArgs

            ss << "(" << handle << ")";
        } else if ( ( strcmp(action, kOfxActionBeginInstanceEdit) == 0) ||
                    ( strcmp(action, kOfxActionEndInstanceEdit) == 0) ) {
            // no inArgs

            ss << "(" << handle << ")";
        } else if (strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
            // inArgs has the following property...
            //     kOfxImageEffectPropContext the context being described.

            // get the context from the inArgs handle
            char *context = 0;
            gPropHost[nth]->propGetString(inArgs, kOfxImageEffectPropContext, 0, &context);

            ss << "(" << handle << "," << context << ")";
#ifdef OFX_DEBUG_PROXY_CLIPS
            gContexts[(OfxImageEffectHandle)handle] = context;
#endif
        } else if (strcmp(action, kOfxImageEffectActionGetRegionOfDefinition) == 0) {
            // inArgs has the following properties...
            //    kOfxPropTime the effect time for which a region of definition is being requested,
            //    kOfxImageEffectPropRenderScale the render scale that should be used in any calculations in this action,

            // get the time
            OfxTime time = 0;
            gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);
            // get the render scale
            double renderScale[2] = {0., 0.};
            gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, renderScale);

            ss << "(" << handle << "," << time << ",(" << renderScale[0] << "," << renderScale[1] << "))";
        } else if (strcmp(action, kOfxImageEffectActionGetRegionsOfInterest) == 0) {
            // inArgs has the following properties...
            //     kOfxPropTime the effect time for which a region of definition is being requested,
            //     kOfxImageEffectPropRenderScale the render scale that should be used in any calculations in this action,
            //     kOfxImageEffectPropRegionOfInterest the region to be rendered in the output image, in Canonical Coordinates.

            // get the time
            OfxTime time = 0;
            gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);
            // get the render scale
            double renderScale[2] = {0., 0.};
            gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, renderScale);
            ss << "(" << handle << "," << time << ",(" << renderScale[0] << "," << renderScale[1] << "))";
            // get the RoI the effect is interested in from inArgs
            double roi[4] = {0., 0., 0., 0.};
            gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRegionOfInterest, 4, roi);

            ss << "(" << handle << "," << time << ",(" << renderScale[0] << "," << renderScale[1] << "),(" << roi[0] << "," << roi[1] << "," << roi[2] << "," << roi[3] << "))";
        } else if (strcmp(action, kOfxImageEffectActionGetFramesNeeded) == 0) {
            // inArgs has the following property...
            //     kOfxPropTime the effect time for which we need to calculate the frames needed on input

            // get the time
            OfxTime time = 0;
            gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);

            ss << "(" << handle << "," << time << ")";
        } else if (strcmp(action, kOfxImageEffectActionIsIdentity) == 0) {
            // inArgs has the following properties...
            //     kOfxPropTime - the time at which to test for identity
            //     kOfxImageEffectPropFieldToRender - the field to test for identity
            //     kOfxImageEffectPropRenderWindow - the window (in \ref PixelCoordinates) to test for identity under
            //     kOfxImageEffectPropRenderScale - the scale factor being applied to the images being renderred

            // get the time from the inArgs
            OfxTime time = 0;
            gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);
            // get the field from the inArgs handle
            char *field = 0;
            gPropHost[nth]->propGetString(inArgs, kOfxImageEffectPropFieldToRender, 0, &field);
            // get the render window
            int renderWindow[4] = {0, 0, 0, 0};
            gPropHost[nth]->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, renderWindow);
            // get the render scale
            double renderScale[2] = {0., 0.};
            gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, renderScale);

            ss << "(" << handle << "," << time << "," << field << ",(" << renderWindow[0] << "," << renderWindow[1 ] << "," << renderWindow[2] << "," << renderWindow[3] << "),(" << renderScale[0] << "," << renderScale[1] << "))";
        } else if (strcmp(action, kOfxImageEffectActionRender) == 0) {
            // inArgs has the following properties...
            //     kOfxPropTime - the time at which to test for identity
            //     kOfxImageEffectPropFieldToRender - the field to test for identity
            //     kOfxImageEffectPropRenderWindow - the window (in \ref PixelCoordinates) to test for identity under
            //     kOfxImageEffectPropRenderScale - the scale factor being applied to the images being renderred
            //     kOfxImageEffectPropSequentialRenderStatus - whether the effect is currently being rendered in strict frame order on a single instance
            //     kOfxImageEffectPropInteractiveRenderStatus - if the render is in response to a user modifying the effect in an interactive session

            // get the time from the inArgs
            OfxTime time = 0;
            gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);
            // get the field from the inArgs handle
            char *field = 0;
            gPropHost[nth]->propGetString(inArgs, kOfxImageEffectPropFieldToRender, 0, &field);
            // get the render window
            int renderWindow[4] = {0, 0, 0, 0};
            gPropHost[nth]->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, renderWindow);
            // get the render scale
            double renderScale[2] = {0., 0.};
            gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, renderScale);
            // get the sequential render status
            int sequentialrenderstatus = 0;
            gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropSequentialRenderStatus, 0, &sequentialrenderstatus);
            // get the interactive render status
            int interactiverenderstatus = 0;
            gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropInteractiveRenderStatus, 0, &interactiverenderstatus);
            // get the view number
#   if defined(OFX_EXTENSIONS_VEGAS) || defined(OFX_EXTENSIONS_NUKE)
            int view = 0;
#   endif
#   ifdef OFX_EXTENSIONS_VEGAS
            int nViews = 0;
#   endif
#   ifdef OFX_EXTENSIONS_VEGAS
            gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropRenderView, 0, &view);
            gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropViewsToRender, 0, &nViews);
#   endif
#   ifdef OFX_EXTENSIONS_NUKE
            gPropHost[nth]->propGetInt(inArgs, kFnOfxImageEffectPropView, 0, &view);
            // check if this is set on inArgs (this property is called kOfxImageEffectPropPlanesAvailable in the OFX 2.0 standards discussion)
            //int ncomps = 0;
            //gPropHost[nth]->propGetDimension(inArgs, kFnOfxImageEffectPropComponentsPresent, &ncomps);
            //ss << "ncomps=" << ncomps;
#   endif
            ss << "(" << handle << "," << time << "," << field << ",(" << renderWindow[0] << "," << renderWindow[1 ] << "," << renderWindow[2] << "," << renderWindow[3] << "),(" << renderScale[0] << "," << renderScale[1] << ")," << sequentialrenderstatus << "," << interactiverenderstatus
#     if defined(OFX_EXTENSIONS_VEGAS) || defined(OFX_EXTENSIONS_NUKE)
                << "," << view
#     endif
#     ifdef OFX_EXTENSIONS_VEGAS
                << "," << nViews
#     endif
                << ")";
        } else if ( ( strcmp(action, kOfxImageEffectActionBeginSequenceRender) == 0) ||
                    ( strcmp(action, kOfxImageEffectActionEndSequenceRender) == 0) ) {
            // inArgs has the following properties...
            //     kOfxImageEffectPropFrameRange - the range of frames (inclusive) that will be renderred,
            //     kOfxImageEffectPropFrameStep - what is the step between frames, generally set to 1 (for full frame renders) or 0.5 (for fielded renders),
            //     kOfxPropIsInteractive - is this a single frame render due to user interaction in a GUI, or a proper full sequence render.
            //     kOfxImageEffectPropRenderScale - the scale factor to apply to images for this call
            //     kOfxImageEffectPropSequentialRenderStatus - whether the effect is currently being rendered in strict frame order on a single instance
            //     kOfxImageEffectPropInteractiveRenderStatus - if the render is in response to a user modifying the effect in an interactive session

            double range[2] = {0., 0.};
            gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropFrameRange, 2, range);
            double step = 0.;
            gPropHost[nth]->propGetDouble(inArgs, kOfxImageEffectPropFrameStep, 0, &step);
            int isinteractive = 0;
            gPropHost[nth]->propGetInt(inArgs, kOfxPropIsInteractive, 0, &isinteractive);
            // get the render scale
            double renderScale[2] = {0., 0.};
            gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, renderScale);
            // get the sequential render status
            int sequentialrenderstatus = 0;
            gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropSequentialRenderStatus, 0, &sequentialrenderstatus);
            // get the interactive render status
            int interactiverenderstatus = 0;
            gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropInteractiveRenderStatus, 0, &interactiverenderstatus);
#   ifdef OFX_EXTENSIONS_NUKE
            // get the view number
            int view = 0;
            gPropHost[nth]->propGetInt(inArgs, kFnOfxImageEffectPropView, 0, &view);
#   endif

            ss << "(" << handle << ",[" << range[0] << "," << range[1] << "]," << step << "," << isinteractive << ",(" << renderScale[0] << "," << renderScale[1] << ")," << sequentialrenderstatus << "," << interactiverenderstatus
#     ifdef OFX_EXTENSIONS_NUKE
                << "," << view
#     endif
                << ")";
        } else if (strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0) {
            // no inArgs

            ss << "(" << handle << ")";
        } else if (strcmp(action, kOfxImageEffectActionGetTimeDomain) == 0) {
            // no inArgs

            ss << "(" << handle << ")";
        } else {
            // unknown OFX Action
            ss << "(" << handle << ") [UNKNOWN ACTION]";
        }

        std::cout << "OFX DebugProxy: " << ss.str() << std::endl;

        assert(gPluginsMainEntry[nth]);
        st =  gPluginsMainEntry[nth](action, handle, inArgs, outArgs);


        // post-hooks on some actions (e.g. print or modify result)
        // get the outargs
        if (strcmp(action, kOfxActionLoad) == 0) {
            // no outArgs
        } else if (strcmp(action, kOfxActionUnload) == 0) {
            // no outArgs
        } else if (strcmp(action, kOfxActionDescribe) == 0) {
            // no outArgs

            // see if the host supports overlays, in which case check if the plugin set kOfxImageEffectPluginPropOverlayInteractV1
            //int supportsOverlays = 0;
            //gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropSupportsOverlays, 0, &supportsOverlays);
            ImageEffectHostDescription &hostDesc = gHostDescription[nth];
            if (hostDesc.supportsOverlays) {
                OfxImageEffectHandle effect = (OfxImageEffectHandle ) handle;
                // get the property handle for the plugin
                OfxPropertySetHandle effectProps;
                gEffectHost[nth]->getPropertySet(effect, &effectProps);

                // set the property that is the overlay's main entry point for the plugin
                gPropHost[nth]->propGetPointer(effectProps,  kOfxImageEffectPluginPropOverlayInteractV1, 0, (void **) &gPluginsOverlayMain[nth]);
                if (gPluginsOverlayMain[nth] != 0) {
                    gPropHost[nth]->propSetPointer( effectProps, kOfxImageEffectPluginPropOverlayInteractV1, 0,  (void *) overlayMainNthFunc(nth) );
                }
            }
        } else if (strcmp(action, kOfxActionCreateInstance) == 0) {
            // no outArgs
        } else if (strcmp(action, kOfxActionDestroyInstance) == 0) {
            // no outArgs
        } else if ( ( strcmp(action, kOfxActionBeginInstanceChanged) == 0) ||
                    ( strcmp(action, kOfxActionEndInstanceChanged) == 0) ) {
            // no outArgs
        } else if (strcmp(action, kOfxActionInstanceChanged) == 0) {
            // no outArgs
        } else if (strcmp(action, kOfxActionPurgeCaches) == 0) {
            // no outArgs
        } else if (strcmp(action, kOfxActionSyncPrivateData) == 0) {
            // no outArgs
        } else if ( ( strcmp(action, kOfxActionBeginInstanceEdit) == 0) ||
                    ( strcmp(action, kOfxActionEndInstanceEdit) == 0) ) {
            // no outArgs
        } else if (strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
            // no outArgs
        } else if (strcmp(action, kOfxImageEffectActionGetRegionOfDefinition) == 0) {
            // outArgs has the following property which the plug-in may set...
            // kOfxImageEffectPropRegionOfDefinitiong, the calculated region of definition, initially set by the host to the default RoD (see below), in Canonical Coordinates.
            if (st == kOfxStatOK) {
                // get the RoD of the effect from outArgs
                double rod[4] = {0., 0., 0., 0.};
                gPropHost[nth]->propGetDoubleN(outArgs, kOfxImageEffectPropRegionOfDefinition, 4, rod);
                ssr << "((" << rod[0] << "," << rod[1] << "," << rod[2] << "," << rod[3] << "))";
            }
        } else if (strcmp(action, kOfxImageEffectActionGetRegionsOfInterest) == 0) {
            // outArgs has a set of 4 dimensional double properties, one for each of the input clips to the effect. The properties are each named "OfxImageClipPropRoI_" with the clip name post pended, for example "OfxImageClipPropRoI_Source". These are initialised to the default RoI.
            if (st == kOfxStatOK) {
#ifdef OFX_DEBUG_PROXY_CLIPS
                ssr << '(';
                const std::list<std::string>& clips = gClips[nth][getContext(nth, (OfxImageEffectHandle)handle)];
                bool first = true;
                for (std::list<std::string>::const_iterator it = clips.begin(); it != clips.end(); ++it) {
                    const char* name = it->c_str();
                    std::string clipROIPropName = std::string("OfxImageClipPropRoI_") + name;
                    double roi[4] = {0., 0., 0., 0.};
                    OfxStatus pst = gPropHost[nth]->propGetDoubleN(outArgs, clipROIPropName.c_str(), 4, roi);
                    if (pst == kOfxStatOK) {
                        if (!first) {
                            ssr << ',';
                        }
                        first = false;
                        ssr << name << ":(" << roi[0] << "," << roi[1] << "," << roi[2] << "," << roi[3] << ")";
                    }
                }
                ssr << ')';
#else
                ssr << "(N/A)";
#endif
            }
        } else if (strcmp(action, kOfxImageEffectActionGetFramesNeeded) == 0) {
            // outArgs has a set of properties, one for each input clip, named "OfxImageClipPropFrameRange_" with the name of the clip post-pended. For example "OfxImageClipPropFrameRange_Source". All these properties are multi-dimensional doubles, with the dimension is a multiple of two. Each pair of values indicates a continuous range of frames that is needed on the given input. They are all initalised to the default value.
            if (st == kOfxStatOK) {
#ifdef OFX_DEBUG_PROXY_CLIPS
                ssr << '(';
                const std::list<std::string>& clips = gClips[nth][getContext(nth, (OfxImageEffectHandle)handle)];
                bool firstclip = true;
                for (std::list<std::string>::const_iterator it = clips.begin(); it != clips.end(); ++it) {
                    const char* name = it->c_str();
                    double range[2] = {0., 0.};
                    std::string clipFrameRangePropName = std::string("OfxImageClipPropFrameRange_") + name;
                    int dim = 0;
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
                            gPropHost[nth]->propGetDoubleN(outArgs, clipFrameRangePropName.c_str(), i + 1, &range[1]);
                            if (!firstrange) {
                                ssr << ',';
                            }
                            firstrange = false;
                            ssr << '(' << range[0] << ',' << range[1] << ')';
                        }
                        ssr << ')';
                    }
                }
                ssr << ')';
#else
                ssr << "(N/A)";
#endif
            }
        }
#ifdef OFX_EXTENSIONS_NUKE
        else if (strcmp(action, kFnOfxImageEffectActionGetFrameViewsNeeded) == 0) {
            // outArgs has a set of properties, one for each input clip, named "OfxImageClipPropFrameRange_" with the name of the clip post-pended. For example "OfxImageClipPropFrameRange_Source". All these properties are multi-dimensional doubles, with the dimension is a multiple of two. Each pair of values indicates a continuous range of frames that is needed on the given input. They are all initalised to the default value.
            if (st == kOfxStatOK) {
#ifdef OFX_DEBUG_PROXY_CLIPS
                ssr << '(';
                const std::list<std::string>& clips = gClips[nth][getContext(nth, (OfxImageEffectHandle)handle)];
                bool firstclip = true;
                for (std::list<std::string>::const_iterator it = clips.begin(); it != clips.end(); ++it) {
                    const char* name = it->c_str();
                    double range[2] = {0., 0.};
                    double view = 0;
                    std::string clipFrameRangePropName = std::string("OfxImageClipPropFrameRangeView_") + name;
                    int dim = 0;
                    OfxStatus pst = gPropHost[nth]->propGetDimension(outArgs, clipFrameRangePropName.c_str(), &dim);
                    if (pst == kOfxStatOK) {
                        if (!firstclip) {
                            ssr << ',';
                        }
                        firstclip = false;
                        bool firstrange = true;
                        ssr << name << ":(";
                        for (int i = 0; i < dim; i += 3) {
                            gPropHost[nth]->propGetDoubleN(outArgs, clipFrameRangePropName.c_str(), i, &range[0]);
                            gPropHost[nth]->propGetDoubleN(outArgs, clipFrameRangePropName.c_str(), i + 1, &range[1]);
                            gPropHost[nth]->propGetDoubleN(outArgs, clipFrameRangePropName.c_str(), i + 2, &view);
                            if (!firstrange) {
                                ssr << ',';
                            }
                            firstrange = false;
                            ssr << name << '(' << range[0] << ',' << range[1] << ',' << view << ')';
                        }
                        ssr << ')';
                    }
                }
#else
                ssr << "(N/A)";
#endif
            }
        } else if (strcmp(action, kFnOfxImageEffectActionGetClipComponents) == 0) {
            // outArgs has a set of properties, one for each input clip, named "OfxImageClipPropFrameRange_" with the name of the clip post-pended. For example "OfxImageClipPropFrameRange_Source". All these properties are multi-dimensional doubles, with the dimension is a multiple of two. Each pair of values indicates a continuous range of frames that is needed on the given input. They are all initalised to the default value.
            if (st == kOfxStatOK) {
#ifdef OFX_DEBUG_PROXY_CLIPS
                ssr << '(';
                const std::list<std::string>& clips = gClips[nth][getContext(nth, (OfxImageEffectHandle)handle)];
                bool firstclip = true;
                for (std::list<std::string>::const_iterator it = clips.begin(); it != clips.end(); ++it) {
                    const char* name = it->c_str();
                    std::string clipFrameRangePropName = std::string(kFnOfxImageEffectActionGetClipComponentsPropString) + name;
                    int dim = 0;
                    OfxStatus pst = gPropHost[nth]->propGetDimension(outArgs, clipFrameRangePropName.c_str(), &dim);
                    if (pst == kOfxStatOK) {
                        if (!firstclip) {
                            ssr << ',';
                        }
                        firstclip = false;
                        bool firstrange = true;
                        ssr << name << ":(";
                        char* comp;
                        for (int i = 0; i < dim; ++i) {
                            gPropHost[nth]->propGetString(outArgs, clipFrameRangePropName.c_str(), i, &comp);
                            if (!firstrange) {
                                ssr << ',';
                            }
                            firstrange = false;
                            ssr << comp;
                        }
                        ssr << ')';
                    }
                }
                char* clipName;
                int clipView;
                double clipTime;
                gPropHost[nth]->propGetDouble(outArgs, kFnOfxImageEffectPropPassThroughTime, 0, &clipTime);
                gPropHost[nth]->propGetString(outArgs, kFnOfxImageEffectPropPassThroughClip, 0, &clipName);
                gPropHost[nth]->propGetInt(outArgs, kFnOfxImageEffectPropPassThroughView, 0, &clipView);
                ssr << '(' << clipName << ',' << clipTime << ',' << clipView << ')';
#else
                ssr << "(N/A)";
#endif
            }
        }
#endif // ifdef OFX_EXTENSIONS_NUKE
        else if (strcmp(action, kOfxImageEffectActionIsIdentity) == 0) {
            // outArgs has the following properties which the plugin can set...
            //    kOfxPropName this to the name of the clip that should be used if the effect is an identity transform, defaults to the empty string
            //    kOfxPropTime the time to use from the indicated source clip as an identity image (allowing time slips to happen), defaults to the value in kOfxPropTime in inArgs
            if (st == kOfxStatOK) {
                char *name = 0;
                gPropHost[nth]->propGetString(outArgs, kOfxPropName, 0, &name);
                OfxTime time = 0;
                gPropHost[nth]->propGetDouble(outArgs, kOfxPropTime, 0, &time);
                ssr << "(" << name << "," << time << ")";
            }
        } else if (strcmp(action, kOfxImageEffectActionRender) == 0) {
            // no outArgs
        } else if ( ( strcmp(action, kOfxImageEffectActionBeginSequenceRender) == 0) ||
                    ( strcmp(action, kOfxImageEffectActionEndSequenceRender) == 0) ) {
            // no outArgs
        } else if (strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0) {
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
                const std::list<std::string>& clips = gClips[nth][getContext(nth, (OfxImageEffectHandle)handle)];
                bool firstclip = true;
                for (std::list<std::string>::const_iterator it = clips.begin(); it != clips.end(); ++it) {
                    const char* name = it->c_str();
                    bool firstpref = true;
                    char* components = 0;
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

                    char *depth = 0;
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

                    double par = 0.;
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
#else // ifdef OFX_DEBUG_PROXY_CLIPS
                ssr << "(N/A)";
#endif // ifdef OFX_DEBUG_PROXY_CLIPS
            }
        } else if (strcmp(action, kOfxImageEffectActionGetTimeDomain) == 0) {
            // outArgs has the following property
            //  kOfxImageEffectPropFrameRange - the frame range an effect can produce images for
            if (st == kOfxStatOK) {
                double range[2] = { 0., 0. };
                gPropHost[nth]->propGetDoubleN(outArgs, kOfxImageEffectPropFrameRange, 2, range);

                ssr << "([" << range[0] << "," << range[1] << "])";
            }
        }
    } catch (std::bad_alloc) {
        // catch memory
        std::cout << "OFX DebugProxy Plugin Memory error." << std::endl;

        return kOfxStatErrMemory;
    } catch (const std::exception& e) {
        // standard exceptions
        std::cout << "OFX DebugProxy Plugin error: " << e.what() << std::endl;

        return kOfxStatErrUnknown;
    } catch (int err) {
        // ho hum, gone wrong somehow
        std::cout << "OFX DebugProxy Plugin error: " << err << std::endl;

        return err;
    } catch (...) {
        // everything else
        std::cout << "OFX DebugProxy Plugin error" << std::endl;

        return kOfxStatErrUnknown;
    }

    if ( ssr.str().empty() ) {
        std::cout << "OFX DebugProxy: " << ss.str() << "->" << OFX::StatStr(st) << std::endl;
    } else {
        std::cout << "OFX DebugProxy: " << ss.str() << "->" << OFX::StatStr(st) << ": " << ssr.str() << std::endl;
    }

    return st;
} // pluginMain

template<int nth>
static OfxStatus
pluginMainNth(const char *action,
              const void *handle,
              OfxPropertySetHandle inArgs,
              OfxPropertySetHandle outArgs)
{
    return pluginMain(nth, action, handle, inArgs, outArgs);
}

#define NTHFUNC(nth) \
case nth: \
    return pluginMainNth < nth >

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
const void*
fetchSuiteNth(OfxPropertySetHandle host,
              const char *suiteName,
              int suiteVersion)
{
    const void* suite = NULL;

    try {
        suite = gHost[nth]->fetchSuite(host, suiteName, suiteVersion);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..fetchSuite(" << suiteName << "," << suiteVersion << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..fetchSuite(" << suiteName << "," << suiteVersion << ")->" << suite << std::endl;
    if ( (strcmp(suiteName, kOfxImageEffectSuite) == 0) && (suiteVersion == 1) ) {
        assert(nth < gEffectHost.size() && suite == gEffectHost[nth]);

        return &gEffectProxy[nth];
    }
# ifdef OFX_EXTENSIONS_NUKE
    if ( (strcmp(suiteName, kFnOfxImageEffectPlaneSuite) == 0) && (suiteVersion == 1) ) {
        assert(nth < gImageEffectPlaneV1Host.size() && suite == gImageEffectPlaneV1Host[nth]);

        return &gImageEffectPlaneV1Proxy[nth];
    }
    if ( (strcmp(suiteName, kFnOfxImageEffectPlaneSuite) == 0) && (suiteVersion == 2) ) {
        assert(nth < gImageEffectPlaneV2Host.size() && suite == gImageEffectPlaneV2Host[nth]);

        return &gImageEffectPlaneV2Proxy[nth];
    }
# endif

    return suite;
}

/////////////// getPropertySet proxy
template<int nth>
static OfxStatus
getPropertySetNth(OfxImageEffectHandle imageEffect,
                  OfxPropertySetHandle *propHandle)
{
    OfxStatus st;

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->getPropertySet ?
              gEffectHost[nth]->getPropertySet(imageEffect, propHandle) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..getPropertySet(" << imageEffect << ", " << propHandle << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..getPropertySet(" << imageEffect << ")->" << OFX::StatStr(st) << ": " << *propHandle << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return getPropertySetNth < nth >

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

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->getParamSet ?
              gEffectHost[nth]->getParamSet(imageEffect, paramSet) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..getParamSet(" << imageEffect << ", " << paramSet << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..getParamSet(" << imageEffect << ")->" << OFX::StatStr(st) << ": " << *paramSet << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return getParamSetNth < nth >

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

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->clipDefine ?
              gEffectHost[nth]->clipDefine(imageEffect, name, propertySet) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipDefine(" << imageEffect << ", " << name << ", " << propertySet << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipDefine(" << imageEffect << ", " << name << ")->" << OFX::StatStr(st) << ": " << *propertySet << std::endl;
#ifdef OFX_DEBUG_PROXY_CLIPS
    assert( !gContexts[imageEffect].empty() );
    gClips[nth][gContexts[imageEffect]].push_back(name);
#endif

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return clipDefineNth < nth >

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

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->clipGetHandle ?
              gEffectHost[nth]->clipGetHandle(imageEffect, name, clip, propertySet) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetHandle(" << imageEffect << ", " << name << ", " << clip << ", " << propertySet << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetHandle(" << imageEffect << ", " << name << ")->" << OFX::StatStr(st) << ": (" << *clip;
    if (propertySet) {
        std::cout << ", " << *propertySet;
    }
    std::cout << ")" << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return clipGetHandleNth < nth >

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

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->clipGetPropertySet ?
              gEffectHost[nth]->clipGetPropertySet(clip, propHandle) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetPropertySet(" << clip << ", " << propHandle << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetPropertySet(" << clip << ")->" << OFX::StatStr(st) << ": " << *propHandle << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return clipGetPropertySetNth < nth >

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
                OfxTime time,
                const OfxRectD     *region,
                OfxPropertySetHandle   *imageHandle)
{
    OfxStatus st;

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->clipGetImage ?
              gEffectHost[nth]->clipGetImage(clip, time, region, imageHandle) :
              kOfxStatErrMissingHostFeature);
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
    return clipGetImageNth < nth >

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

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->clipReleaseImage ?
              gEffectHost[nth]->clipReleaseImage(imageHandle) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipReleaseImage(" << imageHandle << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipReleaseImage(" << imageHandle << ")->" << OFX::StatStr(st) << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return clipReleaseImageNth < nth >

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

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->clipGetRegionOfDefinition ?
              gEffectHost[nth]->clipGetRegionOfDefinition(clip, time, bounds) :
              kOfxStatErrMissingHostFeature);
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
    return clipGetRegionOfDefinitionNth < nth >

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

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->abort ?
              gEffectHost[nth]->abort(imageEffect) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..abort(" << imageEffect << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..abort(" << imageEffect << ")->" << st << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return abortNth < nth >

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

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->imageMemoryAlloc ?
              gEffectHost[nth]->imageMemoryAlloc(instanceHandle, nBytes, memoryHandle) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryAlloc(" << instanceHandle << ", " << nBytes << ", " << memoryHandle << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryAlloc(" << instanceHandle << ", " << nBytes << ")->" << OFX::StatStr(st) << ": " << *memoryHandle << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return imageMemoryAllocNth < nth >

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

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->imageMemoryFree ?
              gEffectHost[nth]->imageMemoryFree(memoryHandle) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryFree(" << memoryHandle << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryFree(" << memoryHandle << ")->" << OFX::StatStr(st) << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return imageMemoryFreeNth < nth >

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

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->imageMemoryLock ?
              gEffectHost[nth]->imageMemoryLock(memoryHandle, returnedPtr) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryLock(" << memoryHandle << ", " << returnedPtr << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryLock(" << memoryHandle << ")->" << OFX::StatStr(st) << ": " << *returnedPtr << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return imageMemoryLockNth < nth >

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

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gEffectHost[nth]->imageMemoryUnlock ?
              gEffectHost[nth]->imageMemoryUnlock(memoryHandle) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryUnlock(" << memoryHandle << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..imageMemoryUnlock(" << memoryHandle << ")->" << OFX::StatStr(st) << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return imageMemoryUnlockNth < nth >

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
    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    gHost[nth] = hostStruct;
    gProxy[nth] = *(gHost[nth]);
    gProxy[nth].fetchSuite = fetchSuiteNth<nth>;
    assert(gPluginsSetHost[nth]);
    gPluginsSetHost[nth](&gProxy[nth]);
}

#define NTHFUNC(nth) \
case nth: \
    return setHostNth < nth >

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


#ifdef OFX_EXTENSIONS_NUKE
/////////////////////FnOfxImageEffectPlaneSuiteV1


// clipGetImagePlane proxy

template<int nth>
static OfxStatus
clipGetImagePlaneV1Nth(OfxImageClipHandle clip,
                       OfxTime time,
                       const char   *plane,
                       const OfxRectD *region,
                       OfxPropertySetHandle   *imageHandle)
{
    OfxStatus st;

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gImageEffectPlaneV1Host[nth]->clipGetImagePlane ?
              gImageEffectPlaneV1Host[nth]->clipGetImagePlane(clip, time, plane, region, imageHandle) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetImagePlane(" << clip << ", " << time << ", " << plane << ", " << region << ", " << imageHandle << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetImagePlane(" << clip << ", " << time << ", " << plane << ")->" << OFX::StatStr(st) << ": (";
    if (region) {
        std::cout << "(" << region->x1 << "," << region->y1 << "," << region->x2 << "," << region->y2 << "), ";
    }
    std::cout << *imageHandle << ")" << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return clipGetImagePlaneV1Nth < nth >

static FnOfxImageEffectPlaneSuiteV1clipGetImagePlane
clipGetImagePlaneV1NthFunc(int nth)
{
    switch (nth) {
        NTHFUNC100(0);
        NTHFUNC100(100);
        NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create clipGetImagePlaneV1 for plugin " << nth << std::endl;

    return 0;
}

#undef NTHFUNC

/////////////////////FnOfxImageEffectPlaneSuiteV2


// clipGetImagePlane proxy

template<int nth>
static OfxStatus
clipGetImagePlaneNth(OfxImageClipHandle clip,
                     OfxTime time,
                     int view,
                     const char   *plane,
                     const OfxRectD *region,
                     OfxPropertySetHandle   *imageHandle)
{
    OfxStatus st;

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gImageEffectPlaneV2Host[nth]->clipGetImagePlane ?
              gImageEffectPlaneV2Host[nth]->clipGetImagePlane(clip, time, view, plane, region, imageHandle) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetImagePlane(" << clip << ", " << time << ", " << view << ", " << plane << ", " << region << ", " << imageHandle << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetImagePlane(" << clip << ", " << time << ", " << view << ", " << plane << ")->" << OFX::StatStr(st) << ": (";
    if (region) {
        std::cout << "(" << region->x1 << "," << region->y1 << "," << region->x2 << "," << region->y2 << "), ";
    }
    std::cout << *imageHandle << ")" << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return clipGetImagePlaneNth < nth >

static FnOfxImageEffectPlaneSuiteV2clipGetImagePlane
clipGetImagePlaneNthFunc(int nth)
{
    switch (nth) {
        NTHFUNC100(0);
        NTHFUNC100(100);
        NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create clipGetImagePlane for plugin " << nth << std::endl;

    return 0;
}

#undef NTHFUNC

// clipgetRegionOfDefinition (plane suite V2)

template<int nth>
static OfxStatus
fnClipGetRegionOfDefinitionNth(OfxImageClipHandle clip,
                               OfxTime time,
                               int view,
                               OfxRectD           *bounds)
{
    OfxStatus st;

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gImageEffectPlaneV2Host[nth]->clipGetRegionOfDefinition ?
              gImageEffectPlaneV2Host[nth]->clipGetRegionOfDefinition(clip, time, view, bounds) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetRegionOfDefinition(plane suite)(" << clip << ", " << time << ", " << view << ", " << bounds << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..clipGetRegionOfDefinition(plane suite)(" << clip << ", " << time << ", " << view << ")->" << OFX::StatStr(st);
    if (bounds) {
        std::cout << ": (" << bounds->x1 << "," << bounds->y1 << "," << bounds->x2 << "," << bounds->y2 << ")";
    }
    std::cout << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return fnClipGetRegionOfDefinitionNth < nth >

static FnOfxImageEffectPlaneSuiteV2clipGetRegionOfDefinition
fnClipGetRegionOfDefinitionNthFunc(int nth)
{
    switch (nth) {
        NTHFUNC100(0);
        NTHFUNC100(100);
        NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create clipGetRegionOfDefinition (plane suite v2) for plugin " << nth << std::endl;

    return 0;
}

#undef NTHFUNC

// getViewName

template<int nth>
static OfxStatus
getViewNameNth(OfxImageEffectHandle effect,
               int view,
               const char         **viewName)
{
    OfxStatus st;

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gImageEffectPlaneV2Host[nth]->getViewName ?
              gImageEffectPlaneV2Host[nth]->getViewName(effect, view, viewName) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..getViewName(" << effect << ", " << view << ", " << *viewName << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..getViewName(" << effect << ", " << view << ", " << *viewName << ")->" << OFX::StatStr(st);
    std::cout << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return getViewNameNth < nth >
static FnOfxImageEffectPlaneSuiteV2getViewName
getViewNameNthFunc(int nth)
{
    switch (nth) {
        NTHFUNC100(0);
        NTHFUNC100(100);
        NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create getViewName for plugin " << nth << std::endl;

    return 0;
}

#undef NTHFUNC

template<int nth>
static OfxStatus
getViewCountNth(OfxImageEffectHandle effect,
                int                 *nViews)
{
    OfxStatus st;

    assert( nth < gHost.size() && nth < gPluginsSetHost.size() );
    try {
        st = (gImageEffectPlaneV2Host[nth]->getViewCount ?
              gImageEffectPlaneV2Host[nth]->getViewCount(effect, nViews) :
              kOfxStatErrMissingHostFeature);
    } catch (...) {
        std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..getViewCount(" << effect << ", " << *nViews << "): host exception!" << std::endl;
        throw;
    }

    std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "..getViewCount(" << effect << ", " << *nViews << ")->" << OFX::StatStr(st);
    std::cout << std::endl;

    return st;
}

#define NTHFUNC(nth) \
case nth: \
    return getViewCountNth < nth >
static FnOfxImageEffectPlaneSuiteV2getViewCount
getViewCountNthFunc(int nth)
{
    switch (nth) {
        NTHFUNC100(0);
        NTHFUNC100(100);
        NTHFUNC100(200);
    }
    std::cout << "OFX DebugProxy: Error: cannot create getViewCount for plugin " << nth << std::endl;

    return 0;
}

#undef NTHFUNC

#endif // OFX_EXTENSIONS_NUKE

OFXS_NAMESPACE_ANONYMOUS_EXIT

// the two mandated functions
EXPORT OfxPlugin *
OfxGetPlugin(int nth)
{
    // get the OfxPlugin* from the underlying plugin
    OfxPlugin* plugin = OfxGetPlugin_binary ? OfxGetPlugin_binary(nth) : 0;

    if (plugin == 0) {
        std::cout << "OFX DebugProxy: Error: plugin " << nth << " is NULL" << std::endl;

        return plugin;
    }


    // copy it
    if ( nth + 1 > (int)gPlugins.size() ) {
        gPlugins.resize(nth + 1);
        gPluginsMainEntry.resize(nth + 1);
        gPluginsOverlayMain.resize(nth + 1);
        gPluginsSetHost.resize(nth + 1);
        gHost.resize(nth + 1);
        gProxy.resize(nth + 1);
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

EXPORT int
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
        gImageEffectPlaneV1Host.reserve(gPluginsNb);
        gImageEffectPlaneV1Proxy.reserve(gPluginsNb);
        gImageEffectPlaneV2Host.reserve(gPluginsNb);
        gImageEffectPlaneV2Proxy.reserve(gPluginsNb);
#endif
#ifdef OX_EXTENSIONS_VEGAS
        gVegasProgressHost.reserve(gPluginsNb);
        gVegasStereoscopicImageHost.reserve(gPluginsNb);
        gVegasKeyframeHost.reserve(gPluginsNb);
#endif

        gInteractHost.reserve(gPluginsNb);
    }

    std::cout << "OFX DebugProxy: OfxGetNumberOfPlugins() -> " << gPluginsNb << std::endl;

    return gPluginsNb;
}

