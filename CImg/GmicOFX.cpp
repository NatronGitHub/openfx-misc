/*
 OFX Gmic plugin.
 An OFX wrapper of the Gimp g'mic plug-in.
 
 Copyright (C) 2014 INRIA
 
 Author: Alexandre Gauthier-Foichat <alexandre.gauthier-foichat@inria.fr>

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
 */

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>

#include "ofxImageEffect.h"
#include "ofxProgress.h"
#include "ofxTimeLine.h"


#include "GmicGimpParser.h"

#define kBasePluginIdentifier "net.sf.openfx.GmicPlugin."
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1

///Hard limit, we can't have more plug-ins defined by this file
#define GMIC_OFX_MAX_PLUGINS_COUNT 1000

//The parser of the .gmic definition file
static Gmic::GmicGimpParser gmicParser;

//A vector containing a copy of the list returned by Gmic::GmicGimpParser::getPluginsByDeclarationOrder()
//stored in a vector for random access
static std::vector<Gmic::GmicTreeNode*> pluginsByRandomAccess;

//Same size as pluginsByRandomAccess, corresponds to plugin ids
static std::vector<std::string> pluginsIds;

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

/** @brief A class that lists all the properties of a host */
struct ImageEffectHostDescription {
public:
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

    int maxParameters;
    int maxPages;
    int pageRowCount;
    int pageColumnCount;
};

// pointers to various bits of the host
static std::vector<OfxHost*>                          gHost;
static std::vector<ImageEffectHostDescription>        gHostDescription;
static std::vector<OfxImageEffectSuiteV1*>            gEffectHost;
static std::vector<OfxPropertySuiteV1*>               gPropHost;
static std::vector<OfxParameterSuiteV1*>              gParamHost;
static std::vector<OfxMemorySuiteV1*>                 gMemoryHost;
static std::vector<OfxMultiThreadSuiteV1*>            gThreadHost;
static std::vector<OfxMessageSuiteV1*>                gMessageHost;
static std::vector<OfxMessageSuiteV2*>                gMessageV2Host;
static std::vector<OfxProgressSuiteV1*>               gProgressHost;
static std::vector<OfxTimeLineSuiteV1*>               gTimeLineHost;
static std::vector<OfxInteractSuiteV1*>    gInteractHost;

////////////////////////////////////////////////////////////////////////////////
// the plugin struct
static std::vector<OfxPlugin> gPlugins;
static int gPluginsNb = 0;


namespace OFX {
    
  
    
    /** @brief maps status to a string */
    const char* mapStatusToString(OfxStatus stat)
    {
        switch(stat)
        {
            case kOfxStatOK             : return "kOfxStatOK";
            case kOfxStatFailed         : return "kOfxStatFailed";
            case kOfxStatErrFatal       : return "kOfxStatErrFatal";
            case kOfxStatErrUnknown     : return "kOfxStatErrUnknown";
            case kOfxStatErrMissingHostFeature : return "kOfxStatErrMissingHostFeature";
            case kOfxStatErrUnsupported : return "kOfxStatErrUnsupported";
            case kOfxStatErrExists      : return "kOfxStatErrExists";
            case kOfxStatErrFormat      : return "kOfxStatErrFormat";
            case kOfxStatErrMemory      : return "kOfxStatErrMemory";
            case kOfxStatErrBadHandle   : return "kOfxStatErrBadHandle";
            case kOfxStatErrBadIndex    : return "kOfxStatErrBadIndex";
            case kOfxStatErrValue       : return "kOfxStatErrValue";
            case kOfxStatReplyYes       : return "kOfxStatReplyYes";
            case kOfxStatReplyNo        : return "kOfxStatReplyNo";
            case kOfxStatReplyDefault   : return "kOfxStatReplyDefault";
            case kOfxStatErrImageFormat : return "kOfxStatErrImageFormat";
        }
        return "UNKNOWN STATUS CODE";
    }

namespace Exception {
    
    /** @brief thrown when a suite returns a dud status code
     */
    class Suite : public std::exception {
        protected :
        OfxStatus _status;
        public :
        Suite(OfxStatus s) : _status(s) {}
        OfxStatus status(void) {return _status;}
        operator OfxStatus() {return _status;}
        
        /** @brief reimplemented from std::exception */
        virtual const char * what () const throw () {return mapStatusToString(_status);}
        
    };
    
    /** @brief Exception indicating that a host doesn't know about a property that is should do */
    class PropertyUnknownToHost : public std::exception {
        protected :
        std::string _what;
        public :
        PropertyUnknownToHost(const char *what) : _what(what) {}
        virtual ~PropertyUnknownToHost() throw() {}
        
        /** @brief reimplemented from std::exception */
        virtual const char * what () const throw ()
        {
            return _what.c_str();
        }
    };
    
    /** @brief exception indicating that the host thinks a property has an illegal value */
    class PropertyValueIllegalToHost : public std::exception {
        protected :
        std::string _what;
        public :
        PropertyValueIllegalToHost(const char *what) : _what(what) {}
        virtual ~PropertyValueIllegalToHost() throw() {}
        
        /** @brief reimplemented from std::exception */
        virtual const char * what () const throw ()
        {
            return _what.c_str();
        }
    };
} //namespace Exception
    
    /** @brief Throws an @ref OFX::Exception depending on the status flag passed in */
    void throwSuiteStatusException(OfxStatus stat) throw(OFX::Exception::Suite, std::bad_alloc)
    {
        switch (stat)
        {
            case kOfxStatOK :
            case kOfxStatReplyYes :
            case kOfxStatReplyNo :
            case kOfxStatReplyDefault :
                break;
                
            case kOfxStatErrMemory :
                throw std::bad_alloc();
                
            default :
                throw OFX::Exception::Suite(stat);
        }
    }
    
    void throwHostMissingSuiteException(std::string name) throw(OFX::Exception::Suite)
    {
        throw OFX::Exception::Suite(kOfxStatErrUnsupported);
    }
    
    void throwPropertyException(OfxStatus stat,
                                const std::string &propName) throw(std::bad_alloc,
    OFX::Exception::PropertyUnknownToHost,
    OFX::Exception::PropertyValueIllegalToHost,
    OFX::Exception::Suite) {
        switch (stat)
        {
            case kOfxStatOK :
            case kOfxStatReplyYes :
            case kOfxStatReplyNo :
            case kOfxStatReplyDefault :
                // Throw nothing!
                break;
                
            case kOfxStatErrUnknown :
            case kOfxStatErrUnsupported : // unsupported implies unknow here
                throw OFX::Exception::PropertyUnknownToHost(propName.c_str());
                break;
                
            case kOfxStatErrMemory :
                throw std::bad_alloc();
                break;
                
            case kOfxStatErrValue :
                throw  OFX::Exception::PropertyValueIllegalToHost(propName.c_str());
                break;
                
            case kOfxStatErrBadHandle :
            case kOfxStatErrBadIndex :
            default :
                throwSuiteStatusException(stat);
                break;
        }
    }
    
}

/* fetch our host APIs from the host struct given us
 the plugin's set host function must have been already called
 */
inline OfxStatus
fetchHostSuites(int nth)
{
    assert(nth < (int)gHost.size());
    if (!gHost[nth])
        return kOfxStatErrMissingHostFeature;
    
    if (nth+1 > (int)gEffectHost.size()) {
        gEffectHost.resize(nth+1);
        gPropHost.resize(nth+1);
        gParamHost.resize(nth+1);
        gMemoryHost.resize(nth+1);
        gThreadHost.resize(nth+1);
        gMessageHost.resize(nth+1);
        gMessageV2Host.resize(nth+1);
        gProgressHost.resize(nth+1);
        gTimeLineHost.resize(nth+1);
        gInteractHost.resize(nth+1);

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

    gInteractHost[nth]   = (OfxInteractSuiteV1 *)   gHost[nth]->fetchSuite(gHost[nth]->host, kOfxInteractSuite, 1);
    if (!gEffectHost[nth] || !gPropHost[nth] || !gParamHost[nth] || !gMemoryHost[nth] || !gThreadHost[nth])
        return kOfxStatErrMissingHostFeature;
    return kOfxStatOK;
}

inline OfxStatus
fetchHostDescription(int nth)
{
    assert(nth < (int)gHost.size());
    if (!gHost[nth])
        return kOfxStatErrMissingHostFeature;
    
    if (nth+1 > (int)gHostDescription.size()) {
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
    for (int i=0; i<numComponents; ++i) {
        char *comp;
        st = gPropHost[nth]->propGetString(host, kOfxImageEffectPropSupportedComponents, i, &comp);
        assert (st == kOfxStatOK);
        hostDesc._supportedComponents.push_back(comp);
    }
    int numContexts;
    st = gPropHost[nth]->propGetDimension(host, kOfxImageEffectPropSupportedContexts, &numContexts);
    assert (st == kOfxStatOK);
    for (int i=0; i<numContexts; ++i) {
        char* cont;
        st = gPropHost[nth]->propGetString(host, kOfxImageEffectPropSupportedContexts, i, &cont);
        assert (st == kOfxStatOK);
        hostDesc._supportedContexts.push_back(cont);
    }
    int numPixelDepths;
    st = gPropHost[nth]->propGetDimension(host, kOfxImageEffectPropSupportedPixelDepths, &numPixelDepths);
    assert (st == kOfxStatOK);
    for (int i=0; i<numPixelDepths; ++i) {
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

#define NTHFUNC1000(nth) \
NTHFUNC100(nth); \
NTHFUNC100(nth+100); \
NTHFUNC100(nth+200); \
NTHFUNC100(nth+300); \
NTHFUNC100(nth+400); \
NTHFUNC100(nth+500); \
NTHFUNC100(nth+600); \
NTHFUNC100(nth+700); \
NTHFUNC100(nth+800); \
NTHFUNC100(nth+900)

/** @brief, returns the dimension of the given property from this property set */
int propGetDimension(int nth,OfxPropertySetHandle handle,const char* property, bool throwOnFailure = true)
{
    int dimension;
    OfxStatus stat = gPropHost[nth]->propGetDimension(handle, property, &dimension);
    
    if (throwOnFailure) {
        OFX::throwPropertyException(stat, property);
    }
    return dimension;
}

void propSetPointer(int nth,OfxPropertySetHandle handle,const char* property, void *value, int idx = 0, bool throwOnFailure = true)
{
    OfxStatus stat = gPropHost[nth]->propSetPointer(handle, property, idx, value);
    if (throwOnFailure) {
        OFX::throwPropertyException(stat, property);
    }
}

void propSetInt(int nth,OfxPropertySetHandle handle,const char* property, int value, int idx, bool throwOnFailure = true)
{
    OfxStatus stat = gPropHost[nth]->propSetInt(handle, property, idx, value);
    if (throwOnFailure) {
        OFX::throwPropertyException(stat, property);
    }
}

void propSetDouble(int nth,OfxPropertySetHandle handle,const char* property, double value, int idx, bool throwOnFailure = true)
{
    OfxStatus stat = gPropHost[nth]->propSetDouble(handle, property, idx, value);
    if (throwOnFailure) {
        OFX::throwPropertyException(stat, property);
    }
}

void propSetString(int nth,OfxPropertySetHandle handle,const char* property, const std::string& value, int idx = 0, bool throwOnFailure = true)
{
    OfxStatus stat = gPropHost[nth]->propSetString(handle, property, idx, value.c_str());
    if (throwOnFailure) {
        OFX::throwPropertyException(stat, property);
    }
}


void describePlugin(int nth, OfxPropertySetHandle effectProp)
{
    assert(nth < (int)pluginsByRandomAccess.size());
    Gmic::GmicTreeNode* gmicPlugin = pluginsByRandomAccess[nth];
    
    const std::string& pluginLabel = gmicPlugin->getName();
    propSetString(nth,effectProp,kOfxPropLabel, pluginLabel);
    propSetString(nth,effectProp,kOfxPropShortLabel, pluginLabel, 0, false);
    propSetString(nth,effectProp,kOfxPropLongLabel, pluginLabel, 0, false);
    
    std::string group;
    Gmic::GmicTreeNode* parent = gmicPlugin->getParent();
    while (parent) {
        std::string toInsert;
        if (group.empty()) {
            toInsert = parent->getName();
        } else {
            toInsert = parent->getName() + "/";
        }
        group.insert(0, toInsert);
        parent = parent->getParent();
    }
    propSetString(nth,effectProp,kOfxImageEffectPluginPropGrouping, group);

    //Safe to assume that gmic plug-ins only support the Filter and General contexts.
    propSetString(nth,effectProp,kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextFilter, 0);
    propSetString(nth,effectProp,kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextGeneral, 1);
    
    //CImg accepts float only
    propSetString(nth,effectProp,kOfxImageEffectPropSupportedPixelDepths, kOfxBitDepthFloat  ,0);
    

    propSetInt(nth,effectProp,kOfxImageEffectPluginPropSingleInstance, 0, 0);
    propSetInt(nth,effectProp,kOfxImageEffectPluginPropHostFrameThreading, 0, 0);
    propSetInt(nth,effectProp,kOfxImageEffectPropTemporalClipAccess, 0, 0);
    propSetInt(nth,effectProp,kOfxImageEffectPluginPropFieldRenderTwiceAlways, true, 0);
    propSetInt(nth,effectProp,kOfxImageEffectPropSupportsMultipleClipPARs, false, 0);
    propSetString(nth,effectProp,kOfxImageEffectPluginRenderThreadSafety, kOfxImageEffectRenderFullySafe, 0);
    propSetInt(nth,effectProp,kOfxImageEffectPropSupportsTiles, kSupportsTiles, 0);
    propSetInt(nth,effectProp,kOfxImageEffectPropSupportsMultiResolution, kSupportsMultiResolution, 0);
}

void defineClip(int nth,OfxImageEffectHandle effectHandle,const char* name,bool isMask,bool optional)
{
    OfxPropertySetHandle clipPropsHandle;
    OfxStatus stat = gEffectHost[nth]->clipDefine(effectHandle, name, &clipPropsHandle);
    (void)stat;
    
    propSetString(nth,clipPropsHandle,kOfxImageEffectPropSupportedComponents, kOfxImageComponentAlpha, 0);
    propSetString(nth,clipPropsHandle,kOfxImageEffectPropSupportedComponents, kOfxImageComponentRGB, 1);
    propSetString(nth,clipPropsHandle,kOfxImageEffectPropSupportedComponents, kOfxImageComponentRGBA, 2);
    
    propSetInt(nth,clipPropsHandle,kOfxImageEffectPropTemporalClipAccess, int(false), 0);
    propSetInt(nth,clipPropsHandle,kOfxImageEffectPropSupportsTiles, int(kSupportsTiles), 0);
    propSetInt(nth,clipPropsHandle,kOfxImageClipPropIsMask, int(isMask), 0);

    propSetInt(nth,clipPropsHandle,kOfxImageClipPropOptional, int(optional), 0);

}


void describePluginInContext(int nth,OfxImageEffectHandle effectHandle,OfxParamSetHandle paramSetHandle)
{
    assert(nth < (int)pluginsByRandomAccess.size());
    Gmic::GmicTreeNode* gmicPlugin = pluginsByRandomAccess[nth];
    
    defineClip(nth, effectHandle, kOfxImageEffectSimpleSourceClipName, false, false);
    defineClip(nth, effectHandle, kOfxImageEffectOutputClipName, false, false);
    
    // can Gmic plug-ins accept a mask ?
    
    //Add a global page
    OfxPropertySetHandle pagePropsHandle;
    {
        OfxStatus stat = gParamHost[nth]->paramDefine(paramSetHandle, kOfxParamTypePage, "Controls", &pagePropsHandle);
        OFX::throwSuiteStatusException(stat);
    }
    
    
    //Build parameters
    const std::list<Gmic::ParameterBase*>& gmicParameters = gmicPlugin->getParameters();
    
    for (std::list<Gmic::ParameterBase*>::const_iterator it = gmicParameters.begin(); it != gmicParameters.end(); ++it) {
        
        const std::string& paramLabel = (*it)->getLabel();
        const std::string& paramScriptName = (*it)->getScriptName();
        
        Gmic::IntParam* isInt = dynamic_cast<Gmic::IntParam*>(*it);
        Gmic::ButtonParam* isButton = dynamic_cast<Gmic::ButtonParam*>(*it);
        Gmic::FloatParam* isFloat = dynamic_cast<Gmic::FloatParam*>(*it);
        Gmic::BooleanParam* isBool = dynamic_cast<Gmic::BooleanParam*>(*it);
        Gmic::ChoiceParam* isChoice = dynamic_cast<Gmic::ChoiceParam*>(*it);
        Gmic::StringParam* isString = dynamic_cast<Gmic::StringParam*>(*it);
        Gmic::ColorParam* isColor = dynamic_cast<Gmic::ColorParam*>(*it);
        
        OfxPropertySetHandle paramPropsHandle;

        if (isInt) {
            OfxStatus stat = gParamHost[nth]->paramDefine(paramSetHandle, kOfxParamTypeInteger, paramScriptName.c_str(), &paramPropsHandle);
            OFX::throwSuiteStatusException(stat);
            
            int rangeMin,rangeMax;
            isInt->getRange(&rangeMin, &rangeMax);
            propSetInt(nth,paramPropsHandle,kOfxParamPropMin, rangeMin,0);
            propSetInt(nth,paramPropsHandle,kOfxParamPropMax, rangeMax,0);
            propSetInt(nth,paramPropsHandle,kOfxParamPropDefault, isInt->getDefaultValue(0),0);
            
        } else if (isBool || isButton) { //< treat buttons as booleans because in Gmic they are assumed to be toggable buttons whereas in OFX they are push buttons
            OfxStatus stat = gParamHost[nth]->paramDefine(paramSetHandle, kOfxParamTypeBoolean, paramScriptName.c_str(), &paramPropsHandle);
            OFX::throwSuiteStatusException(stat);
            
            if (isBool) {
                propSetInt(nth,paramPropsHandle,kOfxParamPropDefault, (int)isBool->getDefaultValue(0),0);
            } else {
                propSetInt(nth,paramPropsHandle,kOfxParamPropDefault, 0, 0);
            }
            
        } else if (isFloat) {
            OfxStatus stat = gParamHost[nth]->paramDefine(paramSetHandle, kOfxParamTypeDouble, paramScriptName.c_str(), &paramPropsHandle);
            OFX::throwSuiteStatusException(stat);
            
            double rangeMin,rangeMax;
            isFloat->getRange(&rangeMin, &rangeMax);
            propSetDouble(nth,paramPropsHandle,kOfxParamPropMin, rangeMin,0);
            propSetDouble(nth,paramPropsHandle,kOfxParamPropMax, rangeMax,0);
            propSetDouble(nth,paramPropsHandle,kOfxParamPropDefault, isFloat->getDefaultValue(0),0);
            propSetString(nth,paramPropsHandle,kOfxParamPropDoubleType, kOfxParamDoubleTypePlain);
            propSetString(nth,paramPropsHandle,kOfxParamPropDefaultCoordinateSystem, kOfxParamCoordinatesCanonical);
            propSetDouble(nth,paramPropsHandle,kOfxParamPropIncrement, 1, 0);
            propSetInt(nth,paramPropsHandle,kOfxParamPropDigits, 2, 0);
        } else if (isColor) {
            
            int nDims = isColor->getNDim();
            if (nDims == 3) {
                OfxStatus stat = gParamHost[nth]->paramDefine(paramSetHandle, kOfxParamTypeRGB, paramScriptName.c_str(), &paramPropsHandle);
                OFX::throwSuiteStatusException(stat);
            } else if (nDims == 4) {
                OfxStatus stat = gParamHost[nth]->paramDefine(paramSetHandle, kOfxParamTypeRGBA, paramScriptName.c_str(), &paramPropsHandle);
                OFX::throwSuiteStatusException(stat);
            } else {
                assert(false);
            }
            
            double defaultColor[4];
            for (int i = 0; i < 4; ++i) {
                if (i >= nDims) {
                    defaultColor[i] = 1.;
                } else {
                    defaultColor[i] = isColor->getDefaultValue(i);
                }
            }
            
            for (int i = 0; i < nDims; ++i) {
                propSetDouble(nth,paramPropsHandle,kOfxParamPropDefault, defaultColor[i], i);
            }
            
            propSetString(nth,paramPropsHandle,kOfxParamPropDimensionLabel, "r", 0);
            propSetString(nth,paramPropsHandle,kOfxParamPropDimensionLabel, "g", 1);
            propSetString(nth,paramPropsHandle,kOfxParamPropDimensionLabel, "b", 2);
            if (nDims == 4) {
                propSetString(nth,paramPropsHandle,kOfxParamPropDimensionLabel, "a", 3);
            }
            
        } else if (isChoice) {
            OfxStatus stat = gParamHost[nth]->paramDefine(paramSetHandle, kOfxParamTypeChoice, paramScriptName.c_str(), &paramPropsHandle);
            OFX::throwSuiteStatusException(stat);
        
            
            const std::list<std::string>& options = isChoice->getOptions();

            for (std::list<std::string>::const_iterator it = options.begin(); it != options.end(); ++it) {
                int nCurrentValues = propGetDimension(nth,paramPropsHandle,kOfxParamPropChoiceOption);
                propSetString(nth,paramPropsHandle,kOfxParamPropChoiceOption, *it, nCurrentValues);
            }
            propSetInt(nth,paramPropsHandle,kOfxParamPropDefault, isChoice->getDefaultValue(0),0);
        } else if (isString) {
           
            Gmic::StringParam::StringParamTypeEnum type = isString->getType();
            
            OfxStatus stat = gParamHost[nth]->paramDefine(paramSetHandle, kOfxParamTypeString, paramScriptName.c_str(), &paramPropsHandle);
            OFX::throwSuiteStatusException(stat);

            switch (type) {
                case Gmic::StringParam::eStringParamTypeFile:
                    propSetString(nth,paramPropsHandle,kOfxParamPropStringMode,  kOfxParamStringIsFilePath);
                    
                    //Assume that the file exists (this will use a "Open" dialog instead of a "Save" dialog). Gmic has no way to differentiate them.
                    propSetInt(nth,paramPropsHandle,kOfxParamPropStringFilePathExists, int(false),0);
                    break;
                case Gmic::StringParam::eStringParamTypeFolder:
                    propSetString(nth,paramPropsHandle,kOfxParamPropStringMode,  kOfxParamStringIsDirectoryPath);
                    break;
                case Gmic::StringParam::eStringParamTypeLabel:
                case Gmic::StringParam::eStringParamTypeUrl:
                    propSetString(nth,paramPropsHandle,kOfxParamPropStringMode,  kOfxParamStringIsLabel);
                    break;
                case Gmic::StringParam::eStringParamTypeMultiLineText:
                    propSetString(nth,paramPropsHandle,kOfxParamPropStringMode,  kOfxParamStringIsMultiLine);
                    break;
                case Gmic::StringParam::eStringParamTypeText:
                    propSetString(nth,paramPropsHandle,kOfxParamPropStringMode,  kOfxParamStringIsSingleLine);
                    break;
            }
            
            propSetString(nth,paramPropsHandle,kOfxParamPropDefault, isString->getDefaultValue(0));
        } else {
            //UnSupported param type
            assert(false);
        }
        
        propSetString(nth,paramPropsHandle,kOfxPropLabel, paramLabel);
        propSetString(nth,paramPropsHandle,kOfxPropShortLabel, paramLabel, false);
        propSetString(nth,paramPropsHandle,kOfxPropLongLabel, paramLabel, false);
        
        propSetInt(nth,paramPropsHandle,kOfxParamPropEvaluateOnChange, int(!(*it)->isSilent()), 0);

        //Add to the main page
        int nKids = propGetDimension(nth,pagePropsHandle,kOfxParamPropPageChild);
        propSetString(nth,pagePropsHandle,kOfxParamPropPageChild, paramScriptName, nKids);
    }
    
}

////////////////////////////////////////////////////////////////////////////////
// The main function
static OfxStatus
pluginMain(int nth, const char *action, const void *rawHandle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
    // fetch the host suites and setup proxies
    if (strcmp(action, kOfxActionLoad) == 0) {
        // fetch the host APIs
        OfxStatus stat;
        if ((stat = fetchHostSuites(nth)) != kOfxStatOK)
            return stat;
        if ((stat = fetchHostDescription(nth)) != kOfxStatOK)
            return stat;
    }
    
    //Cast to the original type
    OfxImageEffectHandle handle = (OfxImageEffectHandle)rawHandle;
    
    OfxStatus st = kOfxStatErrUnknown;
    try {
        // pre-hooks on some actions (e.g. print or modify parameters)
        if (strcmp(action, kOfxActionLoad) == 0) {
            // no inArgs
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxActionUnload) == 0) {
            // no inArgs
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxActionDescribe) == 0) {
            // no inArgs
            OfxPropertySetHandle pluginDescPropsHandle;
            OfxStatus stat = gEffectHost[nth]->getPropertySet(handle, &pluginDescPropsHandle);
            OFX::throwSuiteStatusException(stat);
        
            describePlugin(nth,pluginDescPropsHandle);
            
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
            // inArgs has the following property...
            //     kOfxImageEffectPropContext the context being described.
            
            // get the context from the inArgs handle
            char *context;
            gPropHost[nth]->propGetString(inArgs, kOfxImageEffectPropContext, 0, &context);
            
            // fetch the param set handle
            OfxParamSetHandle paramSetHandle;
            OfxStatus stat = gEffectHost[nth]->getParamSet(handle, &paramSetHandle);
            OFX::throwSuiteStatusException(stat);
            describePluginInContext(nth,handle,paramSetHandle);
            
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxActionCreateInstance) == 0) {
            // no inArgs
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxActionDestroyInstance) == 0) {
            // no inArgs
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxActionBeginInstanceChanged) == 0 ||
                 strcmp(action, kOfxActionEndInstanceChanged) == 0) {
            // inArgs has the following properties...
            //     kOfxPropChangeReason - what triggered the change, which will be one of...
            
            // see why it changed
            char *changeReason;
            gPropHost[nth]->propGetString(inArgs, kOfxPropChangeReason, 0, &changeReason);
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxActionInstanceChanged) == 0) {
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
            
            st = kOfxStatOK;
         
        }
        else if (strcmp(action, kOfxActionPurgeCaches) == 0) {
            // no inArgs
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxActionSyncPrivateData) == 0) {
            // no inArgs
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxActionBeginInstanceEdit) == 0 ||
                 strcmp(action, kOfxActionEndInstanceEdit) == 0) {
            // no inArgs
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxImageEffectActionGetRegionOfDefinition) == 0) {
            // inArgs has the following properties...
            //    kOfxPropTime the effect time for which a region of definition is being requested,
            //    kOfxImageEffectPropRenderScale the render scale that should be used in any calculations in this action,
            
            // get the time
            OfxTime time;
            gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);
            // get the render scale
            OfxPointD renderScale;
            gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, &renderScale.x);
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxImageEffectActionGetRegionsOfInterest) == 0) {
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
            // get the RoI the effect is interested in from inArgs
            OfxRectD roi;
            gPropHost[nth]->propGetDoubleN(inArgs, kOfxImageEffectPropRegionOfInterest, 4, &roi.x1);
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxImageEffectActionGetFramesNeeded) == 0) {
            // inArgs has the following property...
            //     kOfxPropTime the effect time for which we need to calculate the frames needed on input
            
            // get the time
            OfxTime time;
            gPropHost[nth]->propGetDouble(inArgs, kOfxPropTime, 0, &time);
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxImageEffectActionIsIdentity) == 0) {
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
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxImageEffectActionRender) == 0) {
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
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxImageEffectActionBeginSequenceRender) == 0 ||
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
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0) {
            // no inArgs
            st = kOfxStatOK;
        }
        else if (strcmp(action, kOfxImageEffectActionGetTimeDomain) == 0) {
            // no inArgs
            st = kOfxStatOK;
        }
        else {
            // unknown OFX Action
        }
        
        
    } catch (std::bad_alloc) {
        // catch memory
        return kOfxStatErrMemory;
    } catch ( const std::exception& e ) {
        // standard exceptions
        return kOfxStatErrUnknown;
    } catch (int err) {
        // ho hum, gone wrong somehow
        return err;
    } catch ( ... ) {
        // everything else
        return kOfxStatErrUnknown;
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
        NTHFUNC1000(0);
    }
    return 0;
}
#undef NTHFUNC

//////////////// fetchSuite proxy

template<int nth>
const void* fetchSuiteNth(OfxPropertySetHandle host, const char *suiteName, int suiteVersion)
{
    const void* suite = NULL;
    try {
        suite = gHost[nth]->fetchSuite(host, suiteName, suiteVersion);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..fetchSuite(" << suiteName << "," << suiteVersion << "): host exception!" << std::endl;
        throw;
    }
    if (strcmp(suiteName, kOfxImageEffectSuite) == 0 && suiteVersion == 1) {
        assert(nth < gEffectHost.size() && suite == gEffectHost[nth]);
        return &gEffectHost[nth];
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
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->getPropertySet(imageEffect, propHandle);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..getPropertySet(" << imageEffect << ", " << propHandle << "): host exception!" << std::endl;
        throw;
    }
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return getPropertySetNth<nth>

static OfxImageEffectSuiteV1getPropertySet
getPropertySetNthFunc(int nth)
{
    switch (nth) {
       NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create getPropertySet for plugin " << nth << std::endl;
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
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->getParamSet(imageEffect, paramSet);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..getParamSet(" << imageEffect << ", " << paramSet << "): host exception!" << std::endl;
        throw;
    }
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return getParamSetNth<nth>

static OfxImageEffectSuiteV1getParamSet
getParamSetNthFunc(int nth)
{
    switch (nth) {
        NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create getParamSet for plugin " << nth << std::endl;
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
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->clipDefine(imageEffect, name, propertySet);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..clipDefine(" << imageEffect << ", " << name << ", " << propertySet << "): host exception!" << std::endl;
        throw;
    }
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return clipDefineNth<nth>

static OfxImageEffectSuiteV1clipDefine
clipDefineNthFunc(int nth)
{
    switch (nth) {
       NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create clipDefine for plugin " << nth << std::endl;
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
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->clipGetHandle(imageEffect, name, clip, propertySet);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..clipGetHandle(" << imageEffect << ", " << name << ", " << clip << ", " << propertySet << "): host exception!" << std::endl;
        throw;
    }
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return clipGetHandleNth<nth>

static OfxImageEffectSuiteV1clipGetHandle
clipGetHandleNthFunc(int nth)
{
    switch (nth) {
          NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create clipGetHandle for plugin " << nth << std::endl;
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
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->clipGetPropertySet(clip, propHandle);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..clipGetPropertySet(" << clip << ", " << propHandle << "): host exception!" << std::endl;
        throw;
    }
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return clipGetPropertySetNth<nth>

static OfxImageEffectSuiteV1clipGetPropertySet
clipGetPropertySetNthFunc(int nth)
{
    switch (nth) {
         NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create clipGetPropertySet for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// clipGetImage proxy
template<int nth>
static OfxStatus
clipGetImageNth(OfxImageClipHandle clip,
                OfxTime       time,
                const OfxRectD     *region,
                OfxPropertySetHandle   *imageHandle)
{
    OfxStatus st;
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->clipGetImage(clip, time, region, imageHandle);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..clipGetImage(" << clip << ", " << time << ", " << region << ", " << imageHandle << "): host exception!" << std::endl;
        throw;
    }

    return st;
}

#define NTHFUNC(nth) \
case nth: \
return clipGetImageNth<nth>

static OfxImageEffectSuiteV1clipGetImage
clipGetImageNthFunc(int nth)
{
    switch (nth) {
        NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create clipGetImage for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// clipReleaseImage proxy
template<int nth>
static OfxStatus
clipReleaseImageNth(OfxPropertySetHandle imageHandle)
{
    OfxStatus st;
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->clipReleaseImage(imageHandle);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..clipReleaseImage(" << imageHandle << "): host exception!" << std::endl;
        throw;
    }
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return clipReleaseImageNth<nth>

static OfxImageEffectSuiteV1clipReleaseImage
clipReleaseImageNthFunc(int nth)
{
    switch (nth) {
         NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create clipReleaseImage for plugin " << nth << std::endl;
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
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->clipGetRegionOfDefinition(clip, time, bounds);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..clipGetRegionOfDefinition(" << clip << ", " << time << ", " << bounds << "): host exception!" << std::endl;
        throw;
    }
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return clipGetRegionOfDefinitionNth<nth>

static OfxImageEffectSuiteV1clipGetRegionOfDefinition
clipGetRegionOfDefinitionNthFunc(int nth)
{
    switch (nth) {
         NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create clipGetRegionOfDefinition for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// abort proxy
template<int nth>
static int
abortNth(OfxImageEffectHandle imageEffect)
{
    int st;
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->abort(imageEffect);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..abort(" << imageEffect << "): host exception!" << std::endl;
        throw;
    }
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return abortNth<nth>

static OfxImageEffectSuiteV1abort
abortNthFunc(int nth)
{
    switch (nth) {
        NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create abort for plugin " << nth << std::endl;
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
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->imageMemoryAlloc(instanceHandle, nBytes, memoryHandle);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..imageMemoryAlloc(" << instanceHandle << ", " << nBytes << ", " << memoryHandle << "): host exception!" << std::endl;
        throw;
    }
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return imageMemoryAllocNth<nth>

static OfxImageEffectSuiteV1imageMemoryAlloc
imageMemoryAllocNthFunc(int nth)
{
    switch (nth) {
         NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create imageMemoryAlloc for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// imageMemoryFree proxy
template<int nth>
static OfxStatus
imageMemoryFreeNth(OfxImageMemoryHandle memoryHandle)
{
    OfxStatus st;
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->imageMemoryFree(memoryHandle);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..imageMemoryFree(" << memoryHandle << "): host exception!" << std::endl;
        throw;
    }
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return imageMemoryFreeNth<nth>

static OfxImageEffectSuiteV1imageMemoryFree
imageMemoryFreeNthFunc(int nth)
{
    switch (nth) {
        NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create imageMemoryFree for plugin " << nth << std::endl;
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
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->imageMemoryLock(memoryHandle, returnedPtr);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..imageMemoryLock(" << memoryHandle << ", " << returnedPtr << "): host exception!" << std::endl;
        throw;
    }
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return imageMemoryLockNth<nth>

static OfxImageEffectSuiteV1imageMemoryLock
imageMemoryLockNthFunc(int nth)
{
    switch (nth) {
        NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create imageMemoryLock for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

/////////////// imageMemoryUnlock proxy
template<int nth>
static OfxStatus
imageMemoryUnlockNth(OfxImageMemoryHandle memoryHandle)
{
    OfxStatus st;
    assert(nth < gHost.size());
    try {
        st = gEffectHost[nth]->imageMemoryUnlock(memoryHandle);
    } catch (...) {
        std::cout << "GmicOFX: " << gPlugins[nth].pluginIdentifier << "..imageMemoryUnlock(" << memoryHandle << "): host exception!" << std::endl;
        throw;
    }
    return st;
}

#define NTHFUNC(nth) \
case nth: \
return imageMemoryUnlockNth<nth>

static OfxImageEffectSuiteV1imageMemoryUnlock
imageMemoryUnlockNthFunc(int nth)
{
    switch (nth) {
         NTHFUNC1000(0);
    }
    std::cout << "GmicOFX: Error: cannot create imageMemoryUnlock for plugin " << nth << std::endl;
    return 0;
}
#undef NTHFUNC

// function to set the host structure
template<int nth>
static void
setHostNth(OfxHost *hostStruct)
{
    assert(nth < gHost.size());
    gHost[nth] = hostStruct;
}

#define NTHFUNC(nth) \
case nth: \
return setHostNth<nth>

static OfxSetHost*
setHostNthFunc(int nth)
{
    switch (nth) {
         NTHFUNC1000(0);
    }
    return 0;
}
#undef NTHFUNC

static void parseGmicPlugins()
{
    if (gmicParser.getNPlugins() == 0) {
        //The parser has never run, run it for the first time using locally defined plug-ins pointed to by the GMIC_GIMP_PATH
        //HOME (or APPDATA) env vars.
        std::string errors;
        gmicParser.parse(&errors, false);
        
        const std::list<Gmic::GmicTreeNode*>& pluginsByDeclarationOrder = gmicParser.getPluginsByDeclarationOrder();
        
        assert(gmicParser.getNPlugins() == (int)pluginsByDeclarationOrder.size());
        
        pluginsByRandomAccess.resize(std::min(GMIC_OFX_MAX_PLUGINS_COUNT,(int)pluginsByDeclarationOrder.size()));
        pluginsIds.resize(pluginsByRandomAccess.size());
        
        int i = 0;
        for (std::list<Gmic::GmicTreeNode*>::const_iterator it = pluginsByDeclarationOrder.begin(); it != pluginsByDeclarationOrder.end(); ++it,++i) {
            
            //We can't have more plug-ins than that
            if (i >= (int)pluginsByDeclarationOrder.size()) {
                break;
            }
            pluginsByRandomAccess[i] = *it;
            std::string id = std::string(kBasePluginIdentifier) + (*it)->getName();
            pluginsIds[i] = id;
        }
        
        gPluginsNb = gmicParser.getNPlugins();

        gPlugins.resize(gPluginsNb);
        gHost.resize(gPluginsNb);
        gHostDescription.resize(gPluginsNb);
        gEffectHost.resize(gPluginsNb);
        gPropHost.resize(gPluginsNb);
        gParamHost.resize(gPluginsNb);
        gMemoryHost.resize(gPluginsNb);
        gThreadHost.resize(gPluginsNb);
        gMessageHost.resize(gPluginsNb);
        gMessageV2Host.resize(gPluginsNb);
        gProgressHost.resize(gPluginsNb);
        gTimeLineHost.resize(gPluginsNb);
        gInteractHost.resize(gPluginsNb);
    }

}

// the two mandated functions
OfxPlugin *
OfxGetPlugin(int nth)
{
    //The host might have called dlClose() after the call to OfxGetNumberOfPlugins hence destroying the parser's results.
    parseGmicPlugins();
    
    gPlugins[nth].pluginApi = kOfxImageEffectPluginApi;
    gPlugins[nth].apiVersion = 1;
    gPlugins[nth].pluginIdentifier = pluginsIds[nth].c_str();
    gPlugins[nth].pluginVersionMajor = kPluginVersionMajor;
    gPlugins[nth].pluginVersionMinor = kPluginVersionMinor;
    gPlugins[nth].setHost = setHostNthFunc(nth);
    // modify the main entry point
    gPlugins[nth].mainEntry = pluginMainNthFunc(nth);

    return &gPlugins[nth];
}

int
OfxGetNumberOfPlugins(void)
{
    parseGmicPlugins();
    return gPluginsNb;
}



