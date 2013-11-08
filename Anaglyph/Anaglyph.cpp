/*
OFX Anaglyph plugin.
Make an anaglyph image out of the inputs.

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

#ifdef _WINDOWS
#include <windows.h>
#endif

#include <stdio.h>
#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "../include/ofxsProcessing.H"


// Base class for the RGBA and the Alpha processor
class AnaglyphBase : public OFX::ImageProcessor {
protected :
  OFX::Image *_srcLeftImg;
  OFX::Image *_srcRightImg;
  double _amtcolour;
  bool _swap;
  int _offset;
public :
  /** @brief no arg ctor */
  AnaglyphBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcLeftImg(0)
    , _srcRightImg(0)
  {        
  }

  /** @brief set the left src image */
  void setSrcLeftImg(OFX::Image *v) {_srcLeftImg = v;}

  /** @brief set the right src image */
  void setSrcRightImg(OFX::Image *v) {_srcRightImg = v;}

  /** @brief set the amount of colour */
  void setAmtColour(float v) {_amtcolour = v;}

  /** @brief set view swap */
  void setSwap(bool v) {_swap = v;}

  /** @brief set view offset */
  void setOffset(int v) {_offset = v;}
};

// template to do the RGBA processing
template <class PIX, int max>
class ImageAnaglypher : public AnaglyphBase {
public :
  // ctor
  ImageAnaglypher(OFX::ImageEffect &instance) 
    : AnaglyphBase(instance)
  {}

  // and do some processing
  void multiThreadProcessImages(OfxRectI procWindow)
  {
    OFX::Image *srcRedImg = _srcLeftImg;
    OFX::Image *srcCyanImg = _srcRightImg;
    if (_swap) {
      std::swap(srcRedImg, srcCyanImg);
    }
    OfxRectI srcRedBounds = srcRedImg->getBounds();
    OfxRectI srcCyanBounds = srcCyanImg->getBounds();


    for(int y = procWindow.y1; y < procWindow.y2; y++) {
      if(_effect.abort()) break;

      PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

      for(int x = procWindow.x1; x < procWindow.x2; x++) {
        // clamp x to avoid black borders
        int xRed = std::min(std::max(srcRedBounds.x1,x+(_offset+1)/2),srcRedBounds.x2-1);
        int xCyan = std::min(std::max(srcRedBounds.x1,x-_offset/2),srcRedBounds.x2-1);

        PIX *srcRedPix = (PIX *)(srcRedImg ? srcRedImg->getPixelAddress(xRed, y) : 0);
        PIX *srcCyanPix = (PIX *)(srcCyanImg ? srcCyanImg->getPixelAddress(xCyan, y) : 0);

        dstPix[3] = 0; // start with transparent
        if(srcRedPix) {
          PIX srcLuminance = luminance(srcRedPix[0],srcRedPix[1],srcRedPix[2]);
          dstPix[0] = srcLuminance*(1.-_amtcolour) + srcRedPix[0]*_amtcolour;
          dstPix[3] += 0.5*srcRedPix[3];
        }
        else {
          // no src pixel here, be black and transparent
          dstPix[0] = 0;
        }
        if(srcCyanPix) {
          PIX srcLuminance = luminance(srcCyanPix[0],srcCyanPix[1],srcCyanPix[2]);
          dstPix[1] = srcLuminance*(1.-_amtcolour) + srcCyanPix[1]*_amtcolour;
          dstPix[2] = srcLuminance*(1.-_amtcolour) + srcCyanPix[2]*_amtcolour;
          dstPix[3] += 0.5*srcCyanPix[3];
        }
        else {
          // no src pixel here, be black and transparent
          dstPix[1] = 0;
          dstPix[2] = 0;
        }

        // increment the dst pixel
        dstPix += 4;
      }
    }
  }
private :
  /** @brief luminance from linear RGB according to Rec.709.
      See http://www.poynton.com/notes/colour_and_gamma/ColorFAQ.html#RTFToC9 */
  static PIX luminance(PIX red, PIX green, PIX blue) {
    return  PIX(0.2126*red + 0.7152*green + 0.0722*blue);
  }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class AnaglyphPlugin : public OFX::ImageEffect {
protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;

  OFX::DoubleParam  *amtcolour_;
  OFX::BooleanParam *swap_;
  OFX::IntParam     *offset_;

public :
  /** @brief ctor */
  AnaglyphPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , amtcolour_(0)
    , swap_(0)
    , offset_(0)
  {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    amtcolour_  = fetchDoubleParam("amtcolour");
    swap_ = fetchBooleanParam("swap");
    offset_ = fetchIntParam("offset");
  }

  /* Override the render */
  virtual void render(const OFX::RenderArguments &args);

  /* set up and run a processor */
  void setupAndProcess(AnaglyphBase &, const OFX::RenderArguments &args);
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
AnaglyphPlugin::setupAndProcess(AnaglyphBase &processor, const OFX::RenderArguments &args)
{
  // get a dst image
  std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
  OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
  OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

  // fetch main input image
  std::auto_ptr<OFX::Image> srcLeft(srcClip_->fetchStereoscopicImage(args.time,0));
  std::auto_ptr<OFX::Image> srcRight(srcClip_->fetchStereoscopicImage(args.time,1));

  // make sure bit depths are sane
  if(srcLeft.get()) {
    OFX::BitDepthEnum    srcBitDepth      = srcLeft->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = srcLeft->getPixelComponents();

    // see if they have the same depths and bytes and all
    if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
      throw int(1); // HACK!! need to throw an sensible exception here!
  }
  if(srcRight.get()) {
    OFX::BitDepthEnum    srcBitDepth      = srcRight->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = srcRight->getPixelComponents();

    // see if they have the same depths and bytes and all
    if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
      throw int(1); // HACK!! need to throw an sensible exception here!
  }

  double amtcolour = amtcolour_->getValueAtTime(args.time);
  bool swap = swap_->getValue();
  int offset = offset_->getValueAtTime(args.time);

  // set the images
  processor.setDstImg(dst.get());
  processor.setSrcLeftImg(srcLeft.get());
  processor.setSrcRightImg(srcRight.get());

  // set the render window
  processor.setRenderWindow(args.renderWindow);

  // set the parameters
  processor.setAmtColour(amtcolour);
  processor.setSwap(swap);
  processor.setOffset(offset);

  // Call the base class process member, this will call the derived templated process code
  processor.process();
}

// the overridden render function
void
AnaglyphPlugin::render(const OFX::RenderArguments &args)
{
  if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    OFX::throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
  }

  // instantiate the render code based on the pixel depth of the dst clip
  OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
  OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

  // do the rendering
  assert(dstComponents == OFX::ePixelComponentRGBA);

  switch(dstBitDepth) {
    case OFX::eBitDepthUByte : {      
      ImageAnaglypher<unsigned char, 255> fred(*this);
      setupAndProcess(fred, args);
    }
      break;

    case OFX::eBitDepthUShort : {
      ImageAnaglypher<unsigned short, 65535> fred(*this);
      setupAndProcess(fred, args);
    }                          
      break;

    case OFX::eBitDepthFloat : {
      ImageAnaglypher<float, 1> fred(*this);
      setupAndProcess(fred, args);
    }
      break;
  }
}

mDeclarePluginFactory(AnaglyphPluginFactory, ;, {});

using namespace OFX;
void AnaglyphPluginFactory::load()
{
    // we can't be used on hosts that don't support the stereoscopic suite
    if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
        throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    }
}

void AnaglyphPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  // basic labels
  desc.setLabels("Anaglyph", "Anaglyph", "Anaglyph");
  desc.setPluginGrouping("OFX/Views/Stereo");
  desc.setPluginDescription("Make an anaglyph image out of the inputs.");

  // add the supported contexts, only filter at the moment
  desc.addSupportedContext(eContextFilter);

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

  if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
  }
}

void AnaglyphPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
  // Source clip only in the filter context
  // create the mandated source clip
  ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
  srcClip->addSupportedComponent(ePixelComponentRGBA);
  srcClip->setTemporalClipAccess(false);
  srcClip->setSupportsTiles(true);
  srcClip->setIsMask(false);

  // create the mandated output clip
  ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(ePixelComponentRGBA);
  dstClip->setSupportsTiles(true);

  // make some pages and to things in 
  PageParamDescriptor *page = desc.definePageParam("Controls");

  DoubleParamDescriptor *amtcolour = desc.defineDoubleParam("amtcolour");
  amtcolour->setLabels("amtcolour", "amtcolour", "amtcolour");
  amtcolour->setScriptName("amtcolour");
  amtcolour->setHint("Amount of colour in the anaglyph");
  amtcolour->setDefault(0.);
  amtcolour->setRange(0., 1.);
  amtcolour->setIncrement(0.01);
  amtcolour->setDisplayRange(0., 1.);
  amtcolour->setDoubleType(eDoubleTypeScale);
  amtcolour->setAnimates(true);

  page->addChild(*amtcolour);

  BooleanParamDescriptor *swap = desc.defineBooleanParam("swap");
  swap->setDefault(false);
  swap->setHint("Swap left and right views");
  swap->setLabels("(right=red)", "(right=red)", "(right=red)");
  swap->setAnimates(false); // no animation here!

  page->addChild(*swap);

  IntParamDescriptor *offset = desc.defineIntParam("offset");
  offset->setLabels("horizontal offset", "horizontal offset", "horizontal offset");
  offset->setHint("Horizontal offset. "
                  "The red view is shifted to the left by half this amount, " // rounded up
                  "and the cyan view is shifted to the right by half this amount (in pixels)."); // rounded down
  offset->setDefault(0);
  offset->setRange(-1000, 1000);
  offset->setDisplayRange(-100, 100);
  offset->setAnimates(true);

  page->addChild(*offset);
}

OFX::ImageEffect* AnaglyphPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
  return new AnaglyphPlugin(handle);
}

namespace OFX 
{
  namespace Plugin 
  {  
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static AnaglyphPluginFactory p("net.sf.openfx:anaglyphPlugin", 1, 0);
      ids.push_back(&p);
    }
  }
}
