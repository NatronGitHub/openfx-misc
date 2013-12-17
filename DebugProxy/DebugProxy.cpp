/*
Software License :

Copyright (c) 2003, The Open Effects Association Ltd. All rights reserved.

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
*/

/*
  Ofx Example plugin that shows how an overlay is drawn and interacted with.

  It is meant to illustrate certain features of the API, as opposed to being a perfectly
  crafted piece of image processing software.

  Note, that the default bitdepths and components are specified for the source (RGBA + A, 8 + 16 + 32) and that
  this plugin does absolutely no image processing, it assumes the isIdentity action will be called before a render
  and so do nothing.
 */

#ifdef WIN32
#include <windows.h>
#endif
 
//#include <cstring>
//#ifdef __APPLE__
//#include <OpenGL/gl.h>
//#else
//#include <GL/gl.h>
//#endif
//#include <cmath>
#include <stdexcept>
#include <new>
//#include <functional>
#include <cassert>

#include "ofxImageEffect.h"
//#include "ofxMemory.h"
//#include "ofxMultiThread.h"

#include "ofxhBinary.h"
#include "ofxhUtilities.h"

//#include "../include/ofxUtilities.H" // example support utils


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

// pointers to various bits of the host
static std::vector<OfxHost*>               gHost;
static std::vector<OfxImageEffectSuiteV1*> gEffectHost;
static std::vector<OfxPropertySuiteV1*>    gPropHost;
static std::vector<OfxParameterSuiteV1*>   gParamHost;
static std::vector<OfxMemorySuiteV1*>      gMemoryHost;
static std::vector<OfxMultiThreadSuiteV1*> gThreadHost;
static std::vector<OfxMessageSuiteV1*>     gMessageSuite;
static std::vector<OfxInteractSuiteV1*>    gInteractHost;

////////////////////////////////////////////////////////////////////////////////
// the plugin struct 
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
        gBinary = new OFX::Binary(BINARY_PATH);
        gBinary->load();
        // fetch the binary entry points
        OfxGetNumberOfPlugins_binary = (int(*)()) gBinary->findSymbol("OfxGetNumberOfPlugins");
        OfxGetPlugin_binary = (OfxPlugin*(*)(int)) gBinary->findSymbol("OfxGetPlugin");
        std::cout << "OFX DebugProxy: " << BINARY_PATH << " loaded" << std::endl;
    }

    ~Loader()
    {
        gBinary->unload();
        delete gBinary;
        gBinary = 0;
        OfxGetNumberOfPlugins_binary = 0;
        OfxGetPlugin_binary = 0;
        std::cout << "OFX DebugProxy: " << BINARY_PATH << " unloaded" << std::endl;
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
        gPropHost.resize(nth+1);
        gParamHost.resize(nth+1);
        gMemoryHost.resize(nth+1);
        gThreadHost.resize(nth+1);
        gMessageSuite.resize(nth+1);
        gInteractHost.resize(nth+1);
    }

    gEffectHost[nth]   = (OfxImageEffectSuiteV1 *) gHost[nth]->fetchSuite(gHost[nth]->host, kOfxImageEffectSuite, 1);
    gPropHost[nth]     = (OfxPropertySuiteV1 *)    gHost[nth]->fetchSuite(gHost[nth]->host, kOfxPropertySuite, 1);
    gParamHost[nth]    = (OfxParameterSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxParameterSuite, 1);
    gMemoryHost[nth]   = (OfxMemorySuiteV1 *)      gHost[nth]->fetchSuite(gHost[nth]->host, kOfxMemorySuite, 1);
    gThreadHost[nth]   = (OfxMultiThreadSuiteV1 *) gHost[nth]->fetchSuite(gHost[nth]->host, kOfxMultiThreadSuite, 1);
    gMessageSuite[nth]   = (OfxMessageSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxMessageSuite, 1);
    gInteractHost[nth]   = (OfxInteractSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxInteractSuite, 1);
    if(!gEffectHost[nth] || !gPropHost[nth] || !gParamHost[nth] || !gMemoryHost[nth] || !gThreadHost[nth])
        return kOfxStatErrMissingHostFeature;
    return kOfxStatOK;
}

////////////////////////////////////////////////////////////////////////////////
// the entry point for the overlay
static OfxStatus
overlayMain(int nth, const char *action, const void *handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
  std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "." << action << std::endl;
  OfxStatus st = kOfxStatErrUnknown;
  try {
    // pre-hooks on some actions (e.g. print or modify parameters)
    //[NONE]

    st =  gPluginsOverlayMain[nth](action, handle, inArgs, outArgs);

    // post-hooks on some actions (e.g. print or modify result)
    //[NONE]
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
    
  std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "." << action << "->" << OFX::StatStr(st) << std::endl;

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


////////////////////////////////////////////////////////////////////////////////
// The main function
static OfxStatus
pluginMain(int nth, const char *action, const void *handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
  std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "." << action << std::endl;
  OfxStatus st = kOfxStatErrUnknown;
  try {
    // pre-hooks on some actions (e.g. print or modify parameters)
    //[NONE]

    assert(gPluginsMainEntry[nth]);
    st =  gPluginsMainEntry[nth](action, handle, inArgs, outArgs);

      // post-hooks on some actions (e.g. print or modify result)
    if(strcmp(action, kOfxActionDescribe) == 0) {
      // fetch the host APIs
      OfxStatus stat;
      if((stat = fetchHostSuites(nth)) != kOfxStatOK)
        return stat;

      // see if the host supports overlays
      int supportsOverlays;
      gPropHost[nth]->propGetInt(gHost[nth]->host, kOfxImageEffectPropSupportsOverlays, 0, &supportsOverlays);
      if(supportsOverlays != 0) {
        OfxImageEffectHandle effect = (OfxImageEffectHandle ) handle;
        // get the property handle for the plugin
        OfxPropertySetHandle effectProps;
        gEffectHost[nth]->getPropertySet(effect, &effectProps);

        // set the property that is the overlay's main entry point for the plugin
        gPropHost[nth]->propGetPointer(effectProps,  kOfxImageEffectPluginPropOverlayInteractV1, 0, (void **) &gPluginsOverlayMain[nth]);
        gPropHost[nth]->propSetPointer(effectProps, kOfxImageEffectPluginPropOverlayInteractV1, 0,  (void *) overlayMainNthFunc(nth));

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
    
  std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << "." << action << "->" << OFX::StatStr(st) << std::endl;

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


// function to set the host structure
template<int nth>
static void
setHostNth(OfxHost *hostStruct)
{
    assert(nth < gHost.size() && nth < gPluginsSetHost.size());
    gHost[nth] = hostStruct;
    assert(gPluginsSetHost[nth]);
    gPluginsSetHost[nth](hostStruct);
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
  assert(gBinary);

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

  std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << ".OfxGetPlugin-> ("<< nth << ") v"
            << plugin->pluginVersionMajor << "." << plugin->pluginVersionMinor << ")" << std::endl;

  return &gPlugins[nth];
}
 
int
OfxGetNumberOfPlugins(void)
{
  assert(gBinary);

  if (OfxGetNumberOfPlugins_binary == 0) {
     std::cout << "OFX DebugProxy: cannot load plugin from " << BINARY_PATH << std::endl;
     gPluginsNb = 0;
  } else {
    gPluginsNb = (*OfxGetNumberOfPlugins_binary)();
    std::cout << "OFX DebugProxy: found " << gPluginsNb << " plugins in " << BINARY_PATH << std::endl;
    assert(OfxGetPlugin_binary);
    gPlugins.reserve(gPluginsNb);
    gHost.reserve(gPluginsNb);
    gEffectHost.reserve(gPluginsNb);
    gPropHost.reserve(gPluginsNb);
    gParamHost.reserve(gPluginsNb);
    gMemoryHost.reserve(gPluginsNb);
    gThreadHost.reserve(gPluginsNb);
    gMessageSuite.reserve(gPluginsNb);
    gInteractHost.reserve(gPluginsNb);
  }

  return gPluginsNb;
}


