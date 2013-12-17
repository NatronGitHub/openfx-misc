/*
 OFX DebugProxy plugin.
 Intercept and debug communication between an OFX host and an OFX plugin.

 Copyright (C) 2013 INRIA
 Author Frederic Devernay frederic.devernay@inria.fr

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

#ifdef WIN32
#include <windows.h>
#endif
 
#include <sstream>
#include <stdexcept>
#include <new>
#include <cassert>

#include "ofxImageEffect.h"

#include "ofxhBinary.h"
#include "ofxhUtilities.h"


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
  std::stringstream ss;
    ss << gPlugins[nth].pluginIdentifier << ".i." << action;
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
      
      // TODO ss << "(" << handle << ")";
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

      // TODO ss << "(" << handle << ")";
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

      // TODO ss << "(" << handle << ")";
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

      // TODO ss << "(" << handle << ")";
    }


    std::cout << "OFX DebugProxy: " << ss.str() << std::endl;

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
  std::stringstream ss;
    ss << gPlugins[nth].pluginIdentifier << "." << action;
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

    std::cout << "OFX DebugProxy: " << ss.str() << std::endl;
    
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
      gPropHost[nth]->propGetInt(inArgs, kOfxImageEffectPropSupportsOverlays, 0, &supportsOverlays);
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
    
  std::cout << "OFX DebugProxy: " << ss.str() << "->" << OFX::StatStr(st) << std::endl;

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

  std::cout << "OFX DebugProxy: " << gPlugins[nth].pluginIdentifier << ".OfxGetPlugin-> ("<< nth << ", v"
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


