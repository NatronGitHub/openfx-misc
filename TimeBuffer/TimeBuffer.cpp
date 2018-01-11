/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2018 INRIA
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
 * OFX TimeBuffer plugin.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>
//#include <iostream>
#ifdef DEBUG
#include <iostream>
#endif

#include "ofxsCopier.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif

#ifdef OFX_USE_MULTITHREAD_MUTEX
namespace {
typedef MultiThread::Mutex Mutex;
typedef MultiThread::AutoMutex AutoMutex;
}
#else
// some OFX hosts do not have mutex handling in the MT-Suite (e.g. Sony Catalyst Edit)
// prefer using the fast mutex by Marcus Geelnard http://tinythreadpp.bitsnbites.eu/
#include "fast_mutex.h"
namespace {
typedef tthread::fast_mutex Mutex;
typedef OFX::MultiThread::AutoMutexT<tthread::fast_mutex> AutoMutex;
}
#endif

#ifdef DEBUG
#pragma message WARN("TimeBuffer not yet supported by Natron, check again when Natron supports kOfxImageEffectInstancePropSequentialRender")

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginReadName "TimeBufferRead"
#define kPluginReadDescription \
    "Read an time buffer at current time.\n" \
    "A time buffer may be used to get the output of any plugin at a previous time, captured using TimeBufferWrite.\n" \
    "This can typically be used to accumulate several render passes on the same image."
#define kPluginReadIdentifier "net.sf.openfx.TimeBufferRead"
#define kPluginWriteName "TimeBufferWrite"
#define kPluginWriteDescription \
    "Write an time buffer at currect time.\n" \
    "Only one instance may exist with a given accumulation buffer name.\n" \
    "The corresponding TimeBufferRead node, with the same buffer name, must be connected to the 'Sync' input, so that the read operation at the next frame does not start before the write operation at this frame has ended."
#define kPluginWriteIdentifier "net.sf.openfx.TimeBufferWrite"
#define kPluginGrouping "Time"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTilesRead 0
#define kSupportsTilesWrite 0
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kClipSync "Sync"

#define kParamBufferName "bufferName"
#define kParamBufferNameLabel "Buffer Name"
#define kParamBufferNameHint \
    "Name of the buffer.\n" \
    "There must be exactly one TimeBufferRead and one TimeBufferWrite instance using this name, and the output of TimeBufferRead must be connected to the \"Sync\" input of TimeBufferWrite.\n" \
    "This implies that a TimeBufferRead or TimeBufferWrite cannot be duplicated without changing the buffer name, and a unique buffer name must be re-generated when instantiating a PyPlug/Gizmo, or when creating this effect from a script."
#define kParamBufferNameHintNatron \
    "\nNote: In Natron, because OpenFX effects do not know wether they lie in the same project or not, two TimeBufferRead or TimeBufferWrite with the same name can not exist in two projects loaded simultaneously."


#define kParamStartFrame "startFrame"
#define kParamStartFrameLabel "Start Frame"
#define kParamStartFrameHint "First frame of the effect. TimeBufferRead outputs a black and transparent image for this frame and all frames before. The size of the black image is either the size of the Source clip, or the project size if it is not connected."

#define kParamUnorderedRender "unorderedRender"
#define kParamUnorderedRenderLabel "Unordered Render"
#define kParamUnorderedRenderHint \
    "What should be done whenever rendering is not performed in the expected order (i.e. read at t, write at t, read at t+1, etc.).\n" \
    "Any value other than \"Error\" may result in a non-reproductible render. For better safety, \"Error\" should be used for all final renders."
#define kParamUnorderedRenderOptionError "Error", "Do not render anything and return an error. This value should be used for final renders.", "error"
#define kParamUnorderedRenderOptionBlack "Black", "Output a black and transparent image. The size of the black image is either the size of the Source clip, or the project size if it is not connected.", "black"
#define kParamUnorderedRenderOptionLast "Last", "Output the last image, even if it was not produced at the previous frame.", "last"

enum UnorderedRenderEnum
{
    eUnorderedRenderError = 0,
    eUnorderedRenderBlack,
    eUnorderedRenderLast,
};

#define kParamTimeOut "timeOut"
#define kParamTimeOutLabel "Time-out"
#define kParamTimeOutHint "Time-out (in ms) for all operations. Should be larger than the execution time of the whole graphe. 0 means infinite."

#define kParamReset "reset"
#define kParamResetLabel "Reset Buffer"
#define kParamResetHint "Reset the buffer state. Should be done on the TimeBufferRead effect if possible."
#define kParamResetTrigger "resetTrigger" // a dummy parameter to trigger a re-render

#define kParamInfo "info"
#define kParamInfoLabel "Info..."
#define kParamInfoHint "Reset the buffer state."

inline void
sleep(const unsigned int milliseconds)
{
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    Sleep(milliseconds);
#else
    struct timespec tv;
    tv.tv_sec = milliseconds / 1000;
    tv.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&tv, 0);
#endif
}

/*
   We maintain a global map from the buffer name to the buffer data.

   The buffer data contains:
   - an image buffers stored with its valid read time (which is the write time +1), or an invalid date
   - the pointer to the read and the write instances, which should be unique, or NULL if it is not yet created.


   When TimeBufferReadPlugin::render(t) is called:
 * if the write instance does not exist, an error is displayed and render fails
 * if t <= startTime:
   - a black image is rendered
   - if t == startTime, the buffer is locked and marked as dirty, with date t+1, then unlocked
 * if t > startTime:
   - the buffer is locked, and if it doesn't have date t, then either the render fails, a black image is rendered, or the buffer is used anyway, depending on the user-chosen strategy
   - if it is marked as dirty, it is unlocked, then locked and read again after a delay (there are no condition variables in the multithread suite, polling is the only solution). The delay starts at 10ms, and is multiplied by two at each unsuccessful lock. abort() is checked at each iteration.
   - when the buffer is locked and clean, it is copied to output and unlocked
   - the buffer is re-locked for writing, and marked as dirty, with date t+1, then unlocked

   When TimeBufferReadPlugin::getRegionOfDefinition(t) is called:
 * if the write instance does not exist, an error is displayed and render fails
 * if t <= startTime:
   - the RoD is empty
 * if t > startTime:
   - the buffer is locked, and if it doesn't have date t, then either getRoD fails, a black image with an empty RoD is rendered, or the RoD from buffer is used anyway, depending on the user-chosen strategy
   - if it is marked as dirty ,it is unlocked, then locked and read again after a delay (there are no condition variables in the multithread suite, polling is the only solution). The delay starts at 10ms, and is multiplied by two at each unsuccessful lock. abort() is checked at each iteration.
   - when the buffer is locked and clean, the buffer's RoD is returned and it is unlocked


   When TimeBufferWritePlugin::render(t) is called:
   - if the read instance does not exist, an error is displayed and render fails
   - if the "Sync" input is not connected, issue an error message (it should be connected to TimeBufferRead)
   - the buffer is locked for writing, and if it doesn't have date t+1 or is not dirty, then it is unlocked, render fails and a message is posted. It may be because the TimeBufferRead plugin is not upstream - in this case a solution is to connect TimeBufferRead output to TimeBufferWrite' sync input for syncing.
   - src is copied to the buffer, and it is marked as not dirty, then unlocked
   - src is also copied to output.


   There is a "Reset" button both in TimeBufferRead and TimeBufferWrite, which resets the lock and the buffer.

   There is a "Info.." button both in TimeBufferRead and TimeBufferWrite, which gives information about all available buffers.

   If we ever need it, a read-write lock can be implemented using two mutexes, as described in
   https://en.wikipedia.org/wiki/Readers%E2%80%93writer_lock#Using_two_mutexes
   This should not be necessary, since the render action of the read node should be called exactly once per frame.

 */

struct TimeBuffer
{
    ImageEffect *readInstance; // written only once, not protected by mutex
    ImageEffect *writeInstance; // written only once, not protected by mutex
    mutable Mutex mutex;
    double time; // can store any integer from 0 to 2^53
    bool dirty; // TimeBufferRead sets this to true and sets date to t+1, TimeBufferWrite sets this to false
    std::vector<unsigned char> pixelData;
    OfxRectI bounds;
    PixelComponentEnum pixelComponents;
    int pixelComponentCount;
    BitDepthEnum bitDepth;
    int rowBytes;
    OfxPointD renderScale;
    double par;

    TimeBuffer()
        : readInstance(NULL)
        , writeInstance(NULL)
        , mutex()
        , time(-DBL_MAX)
        , dirty(true)
        , pixelData()
        , pixelComponents(ePixelComponentNone)
        , pixelComponentCount(0)
        , bitDepth(eBitDepthNone)
        , rowBytes(0)
        , par(1.)
    {
        bounds.x1 = bounds.y1 = bounds.x2 = bounds.y2 = 0;
        renderScale.x = renderScale.y = 1;
    }
};

// This is the global map from buffer names to buffers.
// The buffer key should *really* be the concatenation of the ProjectId, the GroupId (if any), and the buffer name,
// so that the same name can exist in different groups and/or different projects
typedef std::string TimeBufferKey;
typedef std::map<TimeBufferKey, TimeBuffer*> TimeBufferMap;
static auto_ptr<TimeBufferMap> gTimeBufferMap;
static auto_ptr<Mutex> gTimeBufferMapMutex;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TimeBufferReadPlugin
    : public ImageEffect
{
public:

    /** @brief ctor */
    TimeBufferReadPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _bufferName(NULL)
        , _startFrame(NULL)
        , _unorderedRender(NULL)
        , _timeOut(NULL)
        , _resetTrigger(NULL)
        , _sublabel(NULL)
        , _buffer(NULL)
        , _name()
    {
        setSequentialRender(true); // must also be set here, since it is missing from the plugin descriptor in Resolve
        if ( !gTimeBufferMapMutex.get() ) {
            gTimeBufferMapMutex.reset(new Mutex);
        }
        {
            AutoMutex guard( gTimeBufferMapMutex.get() );
            if ( !gTimeBufferMap.get() ) {
                gTimeBufferMap.reset(new TimeBufferMap);
            }
        }
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGBA) ) );

        _bufferName = fetchStringParam(kParamBufferName);
        _startFrame = fetchIntParam(kParamStartFrame);
        _unorderedRender = fetchChoiceParam(kParamUnorderedRender);
        _timeOut = fetchDoubleParam(kParamTimeOut);
        _resetTrigger = fetchBooleanParam(kParamResetTrigger);
        _sublabel = fetchStringParam(kNatronOfxParamStringSublabelName);
        assert(_bufferName && _startFrame && _unorderedRender && _sublabel);

        std::string name;
        _bufferName->getValue(name);
        setName(name);
        _projectId = getPropertySet().propGetString(kNatronOfxImageEffectPropProjectId, false);
        _groupId = getPropertySet().propGetString(kNatronOfxImageEffectPropGroupId, false);
    }

    virtual ~TimeBufferReadPlugin()
    {
        setName("");
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* Override the clip preferences, we need to say we are setting the frame varying flag */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    void setName(const std::string &name)
    {
        if (name == _name) {
            // ok!
            check();

            return;
        }
        std::string key = _projectId + '.' + _groupId + '.' + name;
        if ( _buffer && (name != _name) ) {
            _buffer->readInstance = 0; // remove reference to this instance
            if (!_buffer->writeInstance) {
                // we may free this buffer
                {
                    AutoMutex guard( gTimeBufferMapMutex.get() );
                    gTimeBufferMap->erase(_name);
                }
                delete _buffer;
                _buffer = 0;
                _name.clear();
            }
        }
        if (!name.empty() && !_buffer) {
            TimeBuffer* timeBuffer = 0;
            {
                AutoMutex guard( gTimeBufferMapMutex.get() );
                TimeBufferMap::const_iterator it = gTimeBufferMap->find(key);
                if ( it != gTimeBufferMap->end() ) {
                    timeBuffer = it->second;
                }
            }
            if ( timeBuffer && timeBuffer->readInstance && (timeBuffer->readInstance != this) ) {
                // a buffer already exists with that name
                setPersistentMessage(Message::eMessageError, "", std::string("A TimeBufferRead already exists with name \"") + name + "\".");
                throwSuiteStatusException(kOfxStatFailed);

                return;
            }
            if (timeBuffer) {
                _buffer = timeBuffer;
            } else {
                _buffer = new TimeBuffer;
            }
            _buffer->readInstance = this;
            _name = name;
            {
                AutoMutex guard( gTimeBufferMapMutex.get() );
                TimeBufferMap::const_iterator it = gTimeBufferMap->find(key);
                if ( it != gTimeBufferMap->end() ) {
                    assert(it->second == timeBuffer);
                    if (it->second != timeBuffer) {
                        setPersistentMessage(Message::eMessageError, "", std::string("A TimeBufferRead already exists with name \"") + name + "\".");
                        delete _buffer;
                        _buffer = 0;
                        _name.clear();
                        throwSuiteStatusException(kOfxStatFailed);

                        return;
                    }
                } else {
                    (*gTimeBufferMap)[key] = _buffer;
                }
            }
        }
        clearPersistentMessage();
        check();
    } // setName

    void check()
    {
#ifdef DEBUG
        std::string key = _projectId + '.' + _groupId + '.' + _name;
        AutoMutex guard( gTimeBufferMapMutex.get() );
        TimeBufferMap::const_iterator it = gTimeBufferMap->find(key);
        if ( it == gTimeBufferMap->end() ) {
            if ( !_name.empty() ) {
                std::cout << "Error: Buffer '" << _name << "' not found\n";

                return;
            }
        } else {
            if ( _name.empty() ) {
                if ( it != gTimeBufferMap->end() ) {
                    std::cout << "Error: Buffer with empty name found\n";
                }
                if (_buffer) {
                    std::cout << "Error: Local buffer with empty name found\n";
                }

                return;
            }
            if (!it->second) {
                std::cout << "Error: Buffer '" << _name << "' is NULL\n";

                return;
            }
            if (it->second->readInstance != this) {
                std::cout << "Error: Buffer '" << _name << "' belongs to " << (void*)it->second->readInstance << ", not " << (void*)this << std::endl;

                return;
            }
        }
#endif
    }

    TimeBuffer* getBuffer()
    {
        std::string key = _projectId + '.' + _groupId + '.' + _name;
        TimeBuffer* timeBuffer = 0;
        // * if the write instance does not exist, an error is displayed and render fails
        {
            AutoMutex guard( gTimeBufferMapMutex.get() );
            TimeBufferMap::const_iterator it = gTimeBufferMap->find(key);
            if ( it != gTimeBufferMap->end() ) {
                timeBuffer = it->second;
            }
        }

        if (!timeBuffer) {
            setPersistentMessage(Message::eMessageError, "", std::string("No TimeBuffer exists with name \"") + _name + "\". Try using another name.");
            throwSuiteStatusException(kOfxStatFailed);

            return NULL;
        }
        if (timeBuffer && !timeBuffer->readInstance) {
            setPersistentMessage(Message::eMessageError, "", std::string("Another TimeBufferRead already exists with name \"") + _name + "\". Try using another name.");
            throwSuiteStatusException(kOfxStatFailed);

            return NULL;
        }
        if ( timeBuffer && timeBuffer->readInstance && (timeBuffer->readInstance != this) ) {
            // a buffer already exists with that name
            setPersistentMessage(Message::eMessageError, "", std::string("Another TimeBufferRead already exists with name \"") + _name + "\". Try using another name.");
            throwSuiteStatusException(kOfxStatFailed);

            return NULL;
        }
        if (timeBuffer && !timeBuffer->writeInstance) {
            setPersistentMessage(Message::eMessageError, "", std::string("No TimeBufferWrite exists with name \"") + _name + "\". Create one and connect it to this TimeBufferRead via the Sync input.");
            throwSuiteStatusException(kOfxStatFailed);

            return NULL;
        }

        return timeBuffer;
    }

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    StringParam *_bufferName;
    IntParam *_startFrame;
    ChoiceParam *_unorderedRender;
    DoubleParam *_timeOut;
    BooleanParam *_resetTrigger;
    StringParam *_sublabel;
    TimeBuffer *_buffer; // associated TimeBuffer
    std::string _name; // name of the TimeBuffer
    std::string _projectId; // identifier for the project the instance lives in
    std::string _groupId; // identifier for the group (or subproject) the instance lives in
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

// the overridden render function
void
TimeBufferReadPlugin::render(const RenderArguments &args)
{
    //std::cout << "render!\n";
    const double time = args.time;

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );

    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();

    assert(dstBitDepth == eBitDepthFloat);
    assert(dstComponents == ePixelComponentRGBA);

    // do the rendering
    TimeBuffer* timeBuffer = 0;
    // * if the write instance does not exist, an error is displayed and render fails
    timeBuffer = getBuffer();
    if (!timeBuffer) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    int startFrame = _startFrame->getValue();
    // * if t <= startTime:
    //   - a black image is rendered
    //   - if t == startTime, the buffer is locked and marked as dirty, with date t+1, then unlocked
    if (time <= startFrame) {
        clearPersistentMessage();
        fillBlack( *this, args.renderWindow, dst.get() );
        if (time == startFrame) {
            AutoMutex guard(timeBuffer->mutex);
            timeBuffer->dirty = true;
            timeBuffer->time = time + 1;
        }
        clearPersistentMessage();

        return;
    }
    AutoMutex guard(timeBuffer->mutex);
    // * if t > startTime:
    //   - the buffer is locked, and if it doesn't have date t, then either the render fails, a black image is rendered, or the buffer is used anyway, depending on the user-chosen strategy
    if (timeBuffer->time != time) {
        UnorderedRenderEnum e = (UnorderedRenderEnum)_unorderedRender->getValue();
        switch (e) {
        case eUnorderedRenderError:
            setPersistentMessage(Message::eMessageError, "", "Frames must be rendered in sequential order");
            throwSuiteStatusException(kOfxStatFailed);

            return;
        case eUnorderedRenderBlack:
            fillBlack( *this, args.renderWindow, dst.get() );
            timeBuffer->dirty = true;
            timeBuffer->time = time + 1;

            return;
        case eUnorderedRenderLast:
            // nothing special to do, continue.
            break;
        }
    }
    //   - if it is marked as dirty, it is unlocked, then locked and read again after a delay (there are no condition variables in the multithread suite, polling is the only solution). The delay starts at 10ms, and is multiplied by two at each unsuccessful lock. abort() is checked at each iteration.
    int delay = 5; // initial delay, in milliseconds
    double timeout = _timeOut->getValue();
    while (timeBuffer->dirty) {
        guard.unlock();
        sleep(delay);
        if ( abort() ) {
            return;
        }
        delay *= 2;
        if (delay > timeout) {
            UnorderedRenderEnum e = (UnorderedRenderEnum)_unorderedRender->getValue();
            switch (e) {
            case eUnorderedRenderError:
            case eUnorderedRenderLast:
                setPersistentMessage(Message::eMessageError, "", "Timed out");
                throwSuiteStatusException(kOfxStatFailed);

                return;
            case eUnorderedRenderBlack:
                fillBlack( *this, args.renderWindow, dst.get() );
                timeBuffer->dirty = true;
                timeBuffer->time = time + 1;

                return;
            }
        }
        guard.relock();
    }
    if ( (args.renderScale.x != timeBuffer->renderScale.x) || (args.renderScale.y != timeBuffer->renderScale.y) ) {
        UnorderedRenderEnum e = (UnorderedRenderEnum)_unorderedRender->getValue();
        switch (e) {
        case eUnorderedRenderError:
        case eUnorderedRenderLast:
            setPersistentMessage(Message::eMessageError, "", "Frames must be rendered in sequential order with the same renderScale");
            throwSuiteStatusException(kOfxStatFailed);

            return;
        case eUnorderedRenderBlack:
            fillBlack( *this, args.renderWindow, dst.get() );
            timeBuffer->dirty = true;
            timeBuffer->time = time + 1;

            return;
        }
    }
    //   - when the buffer is locked and clean, it is copied to output and unlocked
    copyPixels( *this, args.renderWindow,
                (void*)&timeBuffer->pixelData.front(),
                timeBuffer->bounds,
                timeBuffer->pixelComponents,
                timeBuffer->pixelComponentCount,
                timeBuffer->bitDepth,
                timeBuffer->rowBytes,
                dst.get() );
    //   - the buffer is re-locked for writing, and marked as dirty, with date t+1, then unlocked
    timeBuffer->dirty = true;
    timeBuffer->time = time + 1;
    clearPersistentMessage();
    //std::cout << "render! OK\n";
} // TimeBufferReadPlugin::render

/* Override the clip preferences, we need to say we are setting the frame varying flag */
void
TimeBufferReadPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    TimeBuffer* timeBuffer = getBuffer();

    if (!timeBuffer) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    clearPersistentMessage();
    clipPreferences.setOutputFrameVarying(true);
}

bool
TimeBufferReadPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                            OfxRectD &rod)
{
    const double time = args.time;
    TimeBuffer* timeBuffer = 0;

    // * if the write instance does not exist, an error is displayed and render fails
    timeBuffer = getBuffer();
    if (!timeBuffer) {
        throwSuiteStatusException(kOfxStatFailed);

        return false;
    }
    // * if t <= startTime:
    // - the RoD is empty
    int startFrame = _startFrame->getValue();
    if (time <= startFrame) {
        clearPersistentMessage();

        return false; // use default behavior
    }
    AutoMutex guard(timeBuffer->mutex);
    // * if t > startTime:
    // - the buffer is locked, and if it doesn't have date t, then either getRoD fails, a black image with an empty RoD is rendered, or the RoD from buffer is used anyway, depending on the user-chosen strategy
    if (timeBuffer->time != time) {
        UnorderedRenderEnum e = (UnorderedRenderEnum)_unorderedRender->getValue();
        switch (e) {
        case eUnorderedRenderError:
            setPersistentMessage(Message::eMessageError, "", "Frames must be rendered in sequential order");
            throwSuiteStatusException(kOfxStatFailed);

            return false;
        case eUnorderedRenderBlack:

            return false;     // use default behavior
        case eUnorderedRenderLast:
            // nothing special to do, continue.
            break;
        }
    }
    // - if it is marked as dirty ,it is unlocked, then locked and read again after a delay (there are no condition variables in the multithread suite, polling is the only solution). The delay starts at 10ms, and is multiplied by two at each unsuccessful lock. abort() is checked at each iteration.
    int delay = 5; // initial delay, in milliseconds
    double timeout = _timeOut->getValue();
    while (timeBuffer->dirty) {
        guard.unlock();
        sleep(delay);
        if ( abort() ) {
            return false;
        }
        delay *= 2;
        if (delay > timeout) {
            UnorderedRenderEnum e = (UnorderedRenderEnum)_unorderedRender->getValue();
            switch (e) {
            case eUnorderedRenderError:
            case eUnorderedRenderLast:
                setPersistentMessage(Message::eMessageError, "", "Timed out");
                throwSuiteStatusException(kOfxStatFailed);

                return false;
            case eUnorderedRenderBlack:

                return false;     // use default behavior
            }
        }
        guard.relock();
    }
    if ( (args.renderScale.x != timeBuffer->renderScale.x) || (args.renderScale.y != timeBuffer->renderScale.y) ) {
        UnorderedRenderEnum e = (UnorderedRenderEnum)_unorderedRender->getValue();
        switch (e) {
        case eUnorderedRenderError:
        case eUnorderedRenderLast:
            setPersistentMessage(Message::eMessageError, "", "Frames must be rendered in sequential order with the same renderScale");
            throwSuiteStatusException(kOfxStatFailed);

            return false;
        case eUnorderedRenderBlack:
            clearPersistentMessage();

            return false;
        }
    }
    // - when the buffer is locked and clean, the buffer's RoD is returned and it is unlocked
    Coords::toCanonical(timeBuffer->bounds,
                        timeBuffer->renderScale,
                        timeBuffer->par,
                        &rod);
    clearPersistentMessage();

    return true;
} // TimeBufferReadPlugin::getRegionOfDefinition

void
TimeBufferReadPlugin::changedParam(const InstanceChangedArgs & /*args*/,
                                   const std::string &paramName)
{
    if (paramName == kParamBufferName) {
        std::string name;
        _bufferName->getValue(name);
        _sublabel->setValue(name);
        // check if a TimeBufferRead with the same name exists. If yes, issue an error, else clearPersistentMeassage()
        setName(name);
    } else if (paramName == kParamReset) {
        TimeBuffer* timeBuffer = 0;
        // * if the write instance does not exist, an error is displayed and render fails
        timeBuffer = getBuffer();
        if (!timeBuffer) {
            throwSuiteStatusException(kOfxStatFailed);

            return;
        }
        // reset the buffer to a clean state
        AutoMutex guard(timeBuffer->mutex);
        timeBuffer->time = -DBL_MAX;
        timeBuffer->dirty = true;
        timeBuffer->pixelComponents = ePixelComponentNone;
        timeBuffer->pixelComponentCount = 0;
        timeBuffer->bitDepth = eBitDepthNone;
        timeBuffer->rowBytes = 0;
        timeBuffer->par = 1.;
        timeBuffer->bounds.x1 = timeBuffer->bounds.y1 = timeBuffer->bounds.x2 = timeBuffer->bounds.y2 = 0;
        timeBuffer->renderScale.x = timeBuffer->renderScale.y = 1;
        _resetTrigger->setValue( !_resetTrigger->getValue() ); // trigger a render
    } else if (paramName == kParamInfo) {
        // give information about allocated buffers
        // TODO
    }
}

mDeclarePluginFactory(TimeBufferReadPluginFactory, {ofxsThreadSuiteCheck();}, { gTimeBufferMapMutex.reset(NULL); gTimeBufferMap.reset(NULL); });
void
TimeBufferReadPluginFactory::describe(ImageEffectDescriptor &desc)
{
    //std::cout << "describe!\n";
    // basic labels
    desc.setLabel(kPluginReadName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginReadDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextGenerator);
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTilesRead);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    desc.setSequentialRender(true);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
    //std::cout << "describe! OK\n";
}

void
TimeBufferReadPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                               ContextEnum /*context*/)
{
    //std::cout << "describeInContext!\n";
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTilesRead);
    srcClip->setIsMask(false);
    srcClip->setOptional(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTilesRead);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // describe plugin params
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamBufferName);
        param->setLabel(kParamBufferNameLabel);
        if (getImageEffectHostDescription()->isNatron) {
            param->setHint(kParamBufferNameHint kParamBufferNameHintNatron);
        } else {
            param->setHint(kParamBufferNameHint);
        }
        param->setDefault("");
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor* param = desc.defineIntParam(kParamStartFrame);
        param->setLabel(kParamStartFrameLabel);
        param->setHint(kParamStartFrameHint);
        param->setRange(INT_MIN, INT_MAX);
        param->setDisplayRange(INT_MIN, INT_MAX);
        param->setDefault(1);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamUnorderedRender);
        param->setLabel(kParamUnorderedRenderLabel);
        param->setHint(kParamUnorderedRenderHint);
        assert(param->getNOptions() == eUnorderedRenderError);
        param->appendOption(kParamUnorderedRenderOptionError);
        assert(param->getNOptions() == eUnorderedRenderBlack);
        param->appendOption(kParamUnorderedRenderOptionBlack);
        assert(param->getNOptions() == eUnorderedRenderLast);
        param->appendOption(kParamUnorderedRenderOptionLast);
        param->setDefault( (int)eUnorderedRenderError );
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamTimeOut);
        param->setLabel(kParamTimeOutLabel);
        param->setHint(kParamTimeOutHint);
#ifdef DEBUG
        param->setDefault(2000);
#else
        param->setDefault(0);
#endif
        param->setRange(0, DBL_MAX);
        param->setDisplayRange(0, 10000);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamReset);
        param->setLabel(kParamResetLabel);
        param->setHint(kParamResetHint);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamResetTrigger);
        param->setIsSecretAndDisabled(true);
        param->setIsPersistent(false);
        param->setEvaluateOnChange(true);
        if (page) {
            page->addChild(*param);
        }
    }
    /*
       {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamInfo);
        param->setLabel(kParamInfoLabel);
        param->setHint(kParamInfoHint);
        if (page) {
            page->addChild(*param);
        }
       }
     */
    // sublabel
    {
        StringParamDescriptor* param = desc.defineStringParam(kNatronOfxParamStringSublabelName);
        param->setIsSecretAndDisabled(true); // always secret
        param->setIsPersistent(false);
        param->setEvaluateOnChange(false);
        param->setDefault("");
        if (page) {
            page->addChild(*param);
        }
    }
//std::cout << "describeInContext! OK\n";
} // TimeBufferReadPluginFactory::describeInContext

ImageEffect*
TimeBufferReadPluginFactory::createInstance(OfxImageEffectHandle handle,
                                            ContextEnum /*context*/)
{
    return new TimeBufferReadPlugin(handle);
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TimeBufferWritePlugin
    : public ImageEffect
{
public:

    /** @brief ctor */
    TimeBufferWritePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _syncClip(NULL)
        , _bufferName(NULL)
        , _resetTrigger(NULL)
        , _sublabel(NULL)
        , _buffer(NULL)
        , _name()
    {
        if ( !gTimeBufferMapMutex.get() ) {
            gTimeBufferMapMutex.reset(new Mutex);
        }
        {
            AutoMutex guard( gTimeBufferMapMutex.get() );
            if ( !gTimeBufferMap.get() ) {
                gTimeBufferMap.reset(new TimeBufferMap);
            }
        }

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() == ePixelComponentRGBA) );
        _syncClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_syncClip && _syncClip->getPixelComponents() == ePixelComponentRGBA);

        _bufferName = fetchStringParam(kParamBufferName);
        _resetTrigger = fetchBooleanParam(kParamResetTrigger);
        _sublabel = fetchStringParam(kNatronOfxParamStringSublabelName);
        assert(_bufferName && _sublabel);
        std::string name;
        _bufferName->getValue(name);
        setName(name);
        _sublabel->setValue(name);
        _projectId = getPropertySet().propGetString(kNatronOfxImageEffectPropProjectId, false);
        _groupId = getPropertySet().propGetString(kNatronOfxImageEffectPropGroupId, false);
    }

    virtual ~TimeBufferWritePlugin()
    {
        setName("");
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    void setName(const std::string &name)
    {
        if (name == _name) {
            // ok!
            clearPersistentMessage();
            check();

            return;
        }
        std::string key = _projectId + '.' + _groupId + '.' + name;
        if ( _buffer && (name != _name) ) {
            _buffer->writeInstance = 0; // remove reference to this instance
            if (!_buffer->readInstance) {
                // we may free this buffer
                {
                    AutoMutex guard( gTimeBufferMapMutex.get() );
                    gTimeBufferMap->erase(_name);
                }
                delete _buffer;
                _buffer = 0;
                _name.clear();
            }
        }
        if (!name.empty() && !_buffer) {
            TimeBuffer* timeBuffer = 0;
            {
                AutoMutex guard( gTimeBufferMapMutex.get() );
                TimeBufferMap::const_iterator it = gTimeBufferMap->find(key);
                if ( it != gTimeBufferMap->end() ) {
                    timeBuffer = it->second;
                }
            }
            if ( timeBuffer && timeBuffer->writeInstance && (timeBuffer->writeInstance != this) ) {
                // a buffer already exists with that name
                setPersistentMessage(Message::eMessageError, "", std::string("A TimeBufferWrite already exists with name \"") + name + "\".");
                throwSuiteStatusException(kOfxStatFailed);

                return;
            }
            if (timeBuffer) {
                _buffer = timeBuffer;
            } else {
                _buffer = new TimeBuffer;
            }
            _buffer->writeInstance = this;
            _name = name;
            {
                AutoMutex guard( gTimeBufferMapMutex.get() );
                TimeBufferMap::const_iterator it = gTimeBufferMap->find(key);
                if ( it != gTimeBufferMap->end() ) {
                    assert(it->second == timeBuffer);
                    if (it->second != timeBuffer) {
                        setPersistentMessage(Message::eMessageError, "", std::string("A TimeBufferWrite already exists with name \"") + name + "\".");
                        delete _buffer;
                        _buffer = 0;
                        _name.clear();
                        throwSuiteStatusException(kOfxStatFailed);

                        return;
                    }
                } else {
                    (*gTimeBufferMap)[key] = _buffer;
                }
            }
        }
        clearPersistentMessage();
        check();
    } // setName

    void check()
    {
#ifdef DEBUG
        std::string key = _projectId + '.' + _groupId + '.' + _name;
        AutoMutex guard( gTimeBufferMapMutex.get() );
        TimeBufferMap::const_iterator it = gTimeBufferMap->find(key);
        if ( it == gTimeBufferMap->end() ) {
            if ( !_name.empty() ) {
                std::cout << "Error: Buffer '" << _name << "' not found\n";

                return;
            }
        } else {
            if ( _name.empty() ) {
                if ( it != gTimeBufferMap->end() ) {
                    std::cout << "Error: Buffer with empty name found\n";
                }
                if (_buffer) {
                    std::cout << "Error: Local buffer with empty name found\n";
                }

                return;
            }
            if (!it->second) {
                std::cout << "Error: Buffer '" << _name << "' is NULL\n";

                return;
            }
            if (it->second->writeInstance != this) {
                std::cout << "Error: Buffer '" << _name << "' belongs to " << (void*)it->second->writeInstance << ", not " << (void*)this << std::endl;

                return;
            }
        }
#endif
    }

    TimeBuffer* getBuffer()
    {
        std::string key = _projectId + '.' + _groupId + '.' + _name;
        TimeBuffer* timeBuffer = 0;
        // * if the read instance does not exist, an error is displayed and render fails
        {
            AutoMutex guard( gTimeBufferMapMutex.get() );
            TimeBufferMap::const_iterator it = gTimeBufferMap->find(key);
            if ( it != gTimeBufferMap->end() ) {
                timeBuffer = it->second;
            }
        }

        if (!timeBuffer) {
            setPersistentMessage(Message::eMessageError, "", std::string("No TimeBuffer exists with name \"") + _name + "\". Try using another name.");
            throwSuiteStatusException(kOfxStatFailed);

            return NULL;
        }
        if (!timeBuffer->writeInstance) {
            setPersistentMessage(Message::eMessageError, "", std::string("Another TimeBufferWrite already exists with name \"") + _name + "\". Try using another name.");
            throwSuiteStatusException(kOfxStatFailed);

            return NULL;
        }
        if ( timeBuffer->writeInstance && (timeBuffer->writeInstance != this) ) {
            // a buffer already exists with that name
            setPersistentMessage(Message::eMessageError, "", std::string("Another TimeBufferWrite already exists with name \"") + _name + "\". Try using another name.");
            throwSuiteStatusException(kOfxStatFailed);

            return NULL;
        }
        if (!timeBuffer->readInstance) {
            setPersistentMessage(Message::eMessageError, "", std::string("No TimeBufferRead exists with name \"") + _name + "\". Create one and connect it to this TimeBufferWrite via the Sync input.");
            throwSuiteStatusException(kOfxStatFailed);

            return NULL;
        }

        return timeBuffer;
    }

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_syncClip;
    StringParam *_bufferName;
    BooleanParam *_resetTrigger;
    StringParam *_sublabel;
    TimeBuffer *_buffer; // associated TimeBuffer
    std::string _name; // name of the TimeBuffer
    std::string _projectId; // identifier for the project the instance lives in
    std::string _groupId; // identifier for the group (or subproject) the instance lives in
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

// the overridden render function
void
TimeBufferWritePlugin::render(const RenderArguments &args)
{
    //std::cout << "render!\n";
    if ( !_syncClip->isConnected() ) {
        setPersistentMessage(Message::eMessageError, "", "The Sync clip must be connected to the output of the corresponding TimeBufferRead effect.");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering
    // get a dst image
    auto_ptr<Image>  dst( _dstClip->fetchImage(args.time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    assert(dstComponents == ePixelComponentRGBA);
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }

    const double time = args.time;
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    // do the rendering
    TimeBuffer* timeBuffer = 0;
    // - if the read instance does not exist, an error is displayed and render fails
    timeBuffer = getBuffer();
    if (!timeBuffer) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    // - the buffer is locked for writing, and if it doesn't have date t+1 or is not dirty, then it is unlocked, render fails and a message is posted. It may be because the TimeBufferRead plugin is not upstream - in this case a solution is to connect TimeBufferRead output to TimeBufferWrite' sync input for syncing.
    {
        AutoMutex guard(timeBuffer->mutex);
        if ( (timeBuffer->time != time + 1) || !timeBuffer->dirty ) {
            setPersistentMessage(Message::eMessageError, "", "The TimeBuffer has wrong properties. Check that the corresponding TimeBufferRead effect is connected to the Sync input.");
            throwSuiteStatusException(kOfxStatFailed);
        }
        // - src is copied to the buffer, and it is marked as not dirty, then unlocked
        std::vector<unsigned char> pixelData;
        timeBuffer->bounds = args.renderWindow;
        timeBuffer->pixelComponents = src->getPixelComponents();
        timeBuffer->pixelComponentCount = src->getPixelComponentCount();
        timeBuffer->bitDepth = src->getPixelDepth();
        timeBuffer->rowBytes = (args.renderWindow.x2 - args.renderWindow.x1) * timeBuffer->pixelComponentCount * sizeof(float);
        timeBuffer->renderScale = args.renderScale;
        timeBuffer->par = src->getPixelAspectRatio();
        timeBuffer->pixelData.resize( (size_t)timeBuffer->rowBytes * (args.renderWindow.y2 - args.renderWindow.y1) );
        copyPixels(*this, args.renderWindow, src.get(), &timeBuffer->pixelData.front(), timeBuffer->bounds, timeBuffer->pixelComponents, timeBuffer->pixelComponentCount, timeBuffer->bitDepth, timeBuffer->rowBytes);
        timeBuffer->dirty = false;
    }
    // - src is also copied to output.

    copyPixels( *this, args.renderWindow, src.get(), dst.get() );
    clearPersistentMessage();
    //std::cout << "render! OK\n";
} // TimeBufferWritePlugin::render

void
TimeBufferWritePlugin::changedParam(const InstanceChangedArgs & /*args*/,
                                    const std::string &paramName)
{
    if (paramName == kParamBufferName) {
        std::string name;
        _bufferName->getValue(name);
        _sublabel->setValue(name);
        // check if a TimeBufferRead with the same name exists. If yes, issue an error, else clearPersistentMeassage()
        setName(name);
    } else if (paramName == kParamReset) {
        // reset the buffer to a clean state
        TimeBuffer* timeBuffer = 0;
        // * if the write instance does not exist, an error is displayed and render fails
        timeBuffer = getBuffer();
        if (!timeBuffer) {
            throwSuiteStatusException(kOfxStatFailed);

            return;
        }
        // reset the buffer to a clean state
        AutoMutex guard(timeBuffer->mutex);
        if (timeBuffer->readInstance) {
            sendMessage(Message::eMessageError, "", "A TimeBufferRead instance is connected to this buffer, please reset it instead.");

            return;
        }
        timeBuffer->time = -DBL_MAX;
        timeBuffer->dirty = true;
        timeBuffer->pixelComponents = ePixelComponentNone;
        timeBuffer->pixelComponentCount = 0;
        timeBuffer->bitDepth = eBitDepthNone;
        timeBuffer->rowBytes = 0;
        timeBuffer->par = 1.;
        timeBuffer->bounds.x1 = timeBuffer->bounds.y1 = timeBuffer->bounds.x2 = timeBuffer->bounds.y2 = 0;
        timeBuffer->renderScale.x = timeBuffer->renderScale.y = 1;
        _resetTrigger->setValue( !_resetTrigger->getValue() ); // trigger a render
    } else if (paramName == kParamInfo) {
        // give information about allocated buffers
        // TODO
    }
}

mDeclarePluginFactory(TimeBufferWritePluginFactory, {ofxsThreadSuiteCheck();}, { gTimeBufferMapMutex.reset(NULL); gTimeBufferMap.reset(NULL); });
void
TimeBufferWritePluginFactory::describe(ImageEffectDescriptor &desc)
{
    //std::cout << "describe!\n";
    // basic labels
    desc.setLabel(kPluginWriteName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginWriteDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTilesWrite);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    desc.setSequentialRender(true);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif

    //std::cout << "describe! OK\n";
}

void
TimeBufferWritePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                                ContextEnum /*context*/)
{
    //std::cout << "describeInContext!\n";
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTilesWrite);
    srcClip->setIsMask(false);

    ClipDescriptor *syncClip = desc.defineClip(kClipSync);
    syncClip->addSupportedComponent(ePixelComponentRGBA);
    syncClip->setTemporalClipAccess(false);
    syncClip->setSupportsTiles(kSupportsTilesRead);
    syncClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTilesWrite);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // describe plugin params
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamBufferName);
        param->setLabel(kParamBufferNameLabel);
        if (getImageEffectHostDescription()->isNatron) {
            param->setHint(kParamBufferNameHint kParamBufferNameHintNatron);
        } else {
            param->setHint(kParamBufferNameHint);
        }
        param->setDefault("");
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamReset);
        param->setLabel(kParamResetLabel);
        param->setHint(kParamResetHint);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamResetTrigger);
        param->setIsSecretAndDisabled(true);
        param->setIsPersistent(false);
        param->setEvaluateOnChange(true);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    /*
       {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamInfo);
        param->setLabel(kParamInfoLabel);
        param->setHint(kParamInfoHint);
        if (page) {
            page->addChild(*param);
        }
       }
     */
    // sublabel
    {
        StringParamDescriptor* param = desc.defineStringParam(kNatronOfxParamStringSublabelName);
        param->setIsSecretAndDisabled(true); // always secret
        param->setIsPersistent(true);
        param->setEvaluateOnChange(false);
        param->setDefault("");
        if (page) {
            page->addChild(*param);
        }
    }

//std::cout << "describeInContext! OK\n";
} // TimeBufferWritePluginFactory::describeInContext

ImageEffect*
TimeBufferWritePluginFactory::createInstance(OfxImageEffectHandle handle,
                                             ContextEnum /*context*/)
{
    return new TimeBufferWritePlugin(handle);
}

static TimeBufferReadPluginFactory p1(kPluginReadIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static TimeBufferWritePluginFactory p2(kPluginWriteIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)

OFXS_NAMESPACE_ANONYMOUS_EXIT

#endif // DEBUG
