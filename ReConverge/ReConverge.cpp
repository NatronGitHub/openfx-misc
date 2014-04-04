/*
OFX ReConverge plugin.
Shift convergence so that tracked point appears at screen-depth.
The ReConverge node only shifts views horizontally, not vertically.

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

#include "ReConverge.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WINDOWS
#include <windows.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "../include/ofxsProcessing.H"

static const OfxPointD kXHairSize = {5, 5};

class PositionInteract : public OFX::OverlayInteract 
{
 protected :
  enum StateEnum {
    eInActive,
    ePoised,
    ePicked
  };

  StateEnum _state;
  OFX::Double2DParam* _position;
 public :
  PositionInteract(OfxInteractHandle handle, OFX::ImageEffect* effect) 
      : OFX::OverlayInteract(handle)
      , _state(eInActive)
  {
    _position = effect->fetchDouble2DParam("convergepoint");
  }

  // overridden functions from OFX::Interact to do things
  virtual bool draw(const OFX::DrawArgs &args);
  virtual bool penMotion(const OFX::PenArgs &args);
  virtual bool penDown(const OFX::PenArgs &args);
  virtual bool penUp(const OFX::PenArgs &args);
  OfxPointD getCanonicalPosition(double time) const
  {
    OfxPointD offset = _effect->getProjectOffset();
    OfxPointD size = _effect->getProjectSize();
    double x,y;
    _position->getValueAtTime(time, x, y);
    OfxPointD retVal;
    retVal.x = x * size.x + offset.x;
    retVal.y = y * size.y + offset.y;
    return retVal; 
  }
  void setCanonicalPosition(double x, double y, double time)
  {
    OfxPointD offset = _effect->getProjectOffset();
    OfxPointD size = _effect->getProjectSize();
    _position->setValueAtTime(time, (x - offset.x) / size.x, (y - offset.y) / size.y);
  }
};

bool PositionInteract::draw(const OFX::DrawArgs &args)
{
  if (!_position) {
    return false; // nothing to draw
  }
  OfxRGBColourF col;
  switch(_state) {
    case eInActive : col.r = col.g = col.b = 0.0f; break;
    case ePoised   : col.r = col.g = col.b = 0.5f; break;
    case ePicked   : col.r = col.g = col.b = 1.0f; break;
  }

  // make the box a constant size on screen by scaling by the pixel scale
  float dx = (float)(kXHairSize.x / args.pixelScale.x);
  float dy = (float)(kXHairSize.y / args.pixelScale.y);

  OfxPointD pos = getCanonicalPosition(args.time);
  {
    // Draw a shadow for the cross hair
    glPushMatrix();
    // shift by (1,1) pixel
    OfxPointD pos2 = pos;
    pos2.x += 1. / args.pixelScale.x;
    pos2.y += 1. / args.pixelScale.y;
    glColor3f(col.r, col.g, col.b);
    glTranslated(pos2.x, pos2.y, 0);
    glBegin(GL_LINES);
    glVertex2f(-dx, 0);
    glVertex2f(dx, 0);
    glVertex2f(0, -dy);
    glVertex2f(0, dy);
    glEnd();
    glPopMatrix();
  }

  {
    // Draw a cross hair, the current coordinate system aligns with the image plane.
    glPushMatrix();
    // draw the bo
    OfxPointD pos = getCanonicalPosition(args.time);
    glColor3f(col.r, col.g, col.b);
    glTranslated(pos.x, pos.y, 0);
    glBegin(GL_LINES);
    glVertex2f(-dx, 0);
    glVertex2f(dx, 0);
    glVertex2f(0, -dy);
    glVertex2f(0, dy);
    glEnd();
    glPopMatrix();
  }

  return true;
}

// overridden functions from OFX::Interact to do things
bool PositionInteract::penMotion(const OFX::PenArgs &args)
{
  if (!_position) {
    return false;
  }
  // figure the size of the box in cannonical coords
  float dx = (float)(kXHairSize.x / args.pixelScale.x);
  float dy = (float)(kXHairSize.y / args.pixelScale.y);

  OfxPointD pos = getCanonicalPosition(args.time);

  // pen position is in cannonical coords
  OfxPointD penPos = args.penPosition;

  switch(_state) 
  {
    case eInActive : 
    case ePoised   : 
      {
        // are we in the box, become 'poised'
        StateEnum newState;
        penPos.x -= pos.x;
        penPos.y -= pos.y;
        if(std::labs(penPos.x) < dx &&
           std::labs(penPos.y) < dy) {
          newState = ePoised;
        }
        else {
          newState = eInActive;
        }

        if(_state != newState) {
          _state = newState;
          _effect->redrawOverlays();
        }
      }
      break;

    case ePicked   : 
      {
        setCanonicalPosition(penPos.x, penPos.y, args.time);
        _effect->redrawOverlays();
      }
      break;
  }
  return _state != eInActive;
}

bool PositionInteract::penDown(const OFX::PenArgs &args)
{
  if (!_position) {
    return false;
  }
  penMotion(args);
  if(_state == ePoised) {
    _state = ePicked;
    setCanonicalPosition(args.penPosition.x, args.penPosition.y, args.time);
    _effect->redrawOverlays();
  }

  return _state == ePicked;
}

bool PositionInteract::penUp(const OFX::PenArgs &args)
{
  if (!_position) {
    return false;
  }
  if(_state == ePicked) 
  {
    _state = ePoised;
    penMotion(args);
    _effect->redrawOverlays();
    return true;
  }
  return false;
}




// Base class for the RGBA and the Alpha processor
// This class performs a translation by an integer number of pixels (x,y)
class TranslateBase : public OFX::ImageProcessor {
 protected :
  OFX::Image *_srcImg;
  int _translateX;
  int _translateY;
 public :
  /** @brief no arg ctor */
  TranslateBase(OFX::ImageEffect &instance)
      : OFX::ImageProcessor(instance)
      , _srcImg(0)
      , _translateX(0)
      , _translateY(0)
  {        
  }

  /** @brief set the src image */
  void setSrcImg(OFX::Image *v) {_srcImg = v;}

  /** @brief set the translation vector */
  void setTranslate(int x, int y) {_translateX = x; _translateY = y;}
};

// template to do the RGBA processing
template <class PIX, int nComponents, int max>
class ImageTranslator : public TranslateBase {
 public :
  // ctor
  ImageTranslator(OFX::ImageEffect &instance) 
      : TranslateBase(instance)
  {}

  // and do some processing
  void multiThreadProcessImages(OfxRectI procWindow)
  {
    for(int y = procWindow.y1; y < procWindow.y2; y++) {
      if(_effect.abort()) break;

      PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

      for(int x = procWindow.x1; x < procWindow.x2; x++) {

        PIX *srcPix = (PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);

        // do we have a source image to scale up
        if(srcPix) {
          for(int c = 0; c < nComponents; c++) {
            dstPix[c] = max - srcPix[c];
          }
        }
        else {
          // no src pixel here, be black and transparent
          for(int c = 0; c < nComponents; c++) {
            dstPix[c] = 0;
          }
        }

        // increment the dst pixel
        dstPix += nComponents;
      }
    }
  }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ReConvergePlugin : public OFX::ImageEffect {
 protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;
  OFX::Clip *dispClip_;

  OFX::Double2DParam *convergepoint_;
  OFX::IntParam     *offset_;
  OFX::ChoiceParam  *convergemode_;

 public :
  /** @brief ctor */
  ReConvergePlugin(OfxImageEffectHandle handle)
      : ImageEffect(handle)
      , dstClip_(0)
      , srcClip_(0)
  {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    dispClip_ = fetchClip("Disparity");

    convergepoint_ = fetchDouble2DParam("convergepoint");
    offset_ = fetchIntParam("offset");
    convergemode_ = fetchChoiceParam("convergemode");
  }

  /* Override the render */
  virtual void render(const OFX::RenderArguments &args);

  // override the roi call
  virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois);

  /* set up and run a processor */
  void setupAndProcess(TranslateBase &, const OFX::RenderArguments &args);
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
ReConvergePlugin::setupAndProcess(TranslateBase &processor, const OFX::RenderArguments &args)
{
  // get a dst image
  std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
  OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
  OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

  // fetch main input image
  std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));

  // make sure bit depths are sane
  if(src.get()) {
    OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = src->getPixelComponents();

    // see if they have the same depths and bytes and all
    if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
      throw int(1); // HACK!! need to throw an sensible exception here!
  }

  int offset = offset_->getValueAtTime(args.time);
  int convergemode;
  convergemode_->getValue(convergemode);

  // set the images
  processor.setDstImg(dst.get());
  processor.setSrcImg(src.get());

  // set the render window
  processor.setRenderWindow(args.renderWindow);

#pragma message ("TODO")
  // set the parameters
  if (getContext() == OFX::eContextGeneral && convergepoint_ && dispClip_) {
    // fetch the disparity of the tracked point
  }
  //
  switch (convergemode) {
    case 0: // shift left
      break;
    case 1: // shift right
      break;
    case 2: // shift both
      break;
  }

  // Call the base class process member, this will call the derived templated process code
  processor.process();
}

// override the roi call
void 
ReConvergePlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
  // set the ROI of the disp clip to the tracked point position (rounded to the nearest pixel)
  if(getContext() == OFX::eContextGeneral && convergepoint_ && dispClip_) {
    OfxRectD roi;
#pragma message ("TODO")
    rois.setRegionOfInterest(*dispClip_, roi);
  }
}

// the overridden render function
void
ReConvergePlugin::render(const OFX::RenderArguments &args)
{
  // instantiate the render code based on the pixel depth of the dst clip
  OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
  OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

  // do the rendering
  if(dstComponents == OFX::ePixelComponentRGBA) {
    switch(dstBitDepth) {
      case OFX::eBitDepthUByte : {      
        ImageTranslator<unsigned char, 4, 255> fred(*this);
        setupAndProcess(fred, args);
      }
        break;

      case OFX::eBitDepthUShort : {
        ImageTranslator<unsigned short, 4, 65535> fred(*this);
        setupAndProcess(fred, args);
      }                          
        break;

      case OFX::eBitDepthFloat : {
        ImageTranslator<float, 4, 1> fred(*this);
        setupAndProcess(fred, args);
      }
        break;
      default :
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
  } else {
    switch(dstBitDepth) {
      case OFX::eBitDepthUByte : {
        ImageTranslator<unsigned char, 1, 255> fred(*this);
        setupAndProcess(fred, args);
      }
        break;

      case OFX::eBitDepthUShort : {
        ImageTranslator<unsigned short, 1, 65535> fred(*this);
        setupAndProcess(fred, args);
      }                          
        break;

      case OFX::eBitDepthFloat : {
        ImageTranslator<float, 1, 1> fred(*this);
        setupAndProcess(fred, args);
      }                          
        break;
      default :
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
  } 
}

class PositionOverlayDescriptor : public OFX::DefaultEffectOverlayDescriptor<PositionOverlayDescriptor, PositionInteract> {};


using namespace OFX;
void ReConvergePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  // basic labels
  desc.setLabels("ReConvergeOFX", "ReConvergeOFX", "ReConvergeOFX");
  desc.setPluginGrouping("Views/Stereo");
  desc.setPluginDescription("Shift convergence so that a tracked point appears at screen-depth. "
                            "Horizontal disparity may be provided in the red channel of the "
                            "disparity input if it has RGBA components, or the Alpha channel "
                            "if it only has Alpha. "
                            "If no disparity is given, only the offset is taken into account. "
                            "The amount of shift in pixels is rounded to the closest integer. "
                            "The ReConverge node only shifts views horizontally, not vertically.");

  // add the supported contexts
  desc.addSupportedContext(eContextFilter); // parameters are offset and convergemode
  desc.addSupportedContext(eContextGeneral); // adds second input for disparity (in the red channel), and convergepoint (with interact)

  // add supported pixel depths
  desc.addSupportedBitDepth(eBitDepthUByte);
  desc.addSupportedBitDepth(eBitDepthUShort);
  desc.addSupportedBitDepth(eBitDepthFloat);

  // set a few flags
  desc.setSingleInstance(false);
  desc.setHostFrameThreading(false);
  desc.setSupportsMultiResolution(true);
  desc.setSupportsTiles(true);
  desc.setTemporalClipAccess(false);
  desc.setRenderTwiceAlways(false);
  desc.setSupportsMultipleClipPARs(false);

  desc.setOverlayInteractDescriptor( new PositionOverlayDescriptor);
}

void ReConvergePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
  // Source clip only in the filter context
  // create the mandated source clip
  ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
  srcClip->addSupportedComponent(ePixelComponentRGBA);
  srcClip->addSupportedComponent(ePixelComponentAlpha);
  srcClip->setTemporalClipAccess(false);
  srcClip->setSupportsTiles(true);
  srcClip->setIsMask(false);

  if (context == eContextGeneral) {
    // Optional disparity clip
    ClipDescriptor *dispClip = desc.defineClip("Disparity");
    dispClip->addSupportedComponent(ePixelComponentRGBA);
    dispClip->addSupportedComponent(ePixelComponentAlpha);
    dispClip->setTemporalClipAccess(false);
    dispClip->setOptional(true);
    dispClip->setSupportsTiles(true); 
  }

  // create the mandated output clip
  ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(ePixelComponentRGBA);
  dstClip->addSupportedComponent(ePixelComponentAlpha);
  dstClip->setSupportsTiles(true);

  // make some pages and to things in 
  PageParamDescriptor *page = desc.definePageParam("Controls");

  if (context == eContextGeneral) {
    Double2DParamDescriptor *convergepoint = desc.defineDouble2DParam("convergepoint");
    convergepoint->setLabels("Converge upon", "Converge upon", "Converge upon");
    convergepoint->setScriptName("convergepoint");
    convergepoint->setHint("Position of the tracked point when the convergence is set");
    convergepoint->setDoubleType(eDoubleTypeXYAbsolute);
    convergepoint->setDefaultCoordinateSystem(eCoordinatesNormalised);
    convergepoint->setDefault(0.5, 0.5);
    convergepoint->setAnimates(true);
    page->addChild(*convergepoint);
  }

  IntParamDescriptor *offset = desc.defineIntParam("offset");
  offset->setLabels("Convergence offset", "Convergence offset", "Convergence offset");
  offset->setScriptName("offset");
  offset->setHint("The disparity of the tracked point will be set to this");
  offset->setDefault(0);
  offset->setRange(-1000, 1000);
  offset->setDisplayRange(-100, 100);
  offset->setAnimates(true);

  page->addChild(*offset);

  ChoiceParamDescriptor *convergemode = desc.defineChoiceParam("convergemode");
  convergemode->setLabels("Mode", "Mode", "Mode");
  convergemode->setHint("Select to view to be shifted in order to set convergence");
  convergemode->setScriptName("convergemode");
  convergemode->appendOption("shift right", "shift right");
  convergemode->appendOption("shift left", "shift left");
  convergemode->appendOption("shift both", "shift both");
  convergemode->setAnimates(false); // no animation here!

  page->addChild(*convergemode);
}

OFX::ImageEffect* ReConvergePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
  return new ReConvergePlugin(handle);
}

