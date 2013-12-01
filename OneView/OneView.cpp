/*
OFX OneView plugin.
Takes one view from the input.

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
class CopierBase : public OFX::ImageProcessor {
protected :
  OFX::Image *_srcImg;
public :
  /** @brief no arg ctor */
  CopierBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
  {        
  }

  /** @brief set the src image */
  void setSrcImg(OFX::Image *v) {_srcImg = v;}
};

// template to do the RGBA processing
template <class PIX, int nComponents>
class ImageCopier : public CopierBase {
public :
  // ctor
  ImageCopier(OFX::ImageEffect &instance) 
    : CopierBase(instance)
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
            dstPix[c] = srcPix[c];
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
class OneViewPlugin : public OFX::ImageEffect {
protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;

  OFX::ChoiceParam     *view_;

public :
  /** @brief ctor */
  OneViewPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , view_(0)
  {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    view_ = fetchChoiceParam("view");
  }

  /* Override the render */
  virtual void render(const OFX::RenderArguments &args);

  /* set up and run a processor */
  void setupAndProcess(CopierBase &, const OFX::RenderArguments &args);
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
OneViewPlugin::setupAndProcess(CopierBase &processor, const OFX::RenderArguments &args)
{
  // get a dst image
  std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
  OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
  OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

  int view;
  view_->getValue(view);
  // fetch main input image
  std::auto_ptr<OFX::Image> src(srcClip_->fetchStereoscopicImage(args.time,view));

  // make sure bit depths are sane
  if(src.get()) {
    OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = src->getPixelComponents();

    // see if they have the same depths and bytes and all
    if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
      throw int(1); // HACK!! need to throw an sensible exception here!
  }

  // set the images
  processor.setDstImg(dst.get());
  processor.setSrcImg(src.get());

  // set the render window
  processor.setRenderWindow(args.renderWindow);

  // Call the base class process member, this will call the derived templated process code
  processor.process();
}

// the overridden render function
void
OneViewPlugin::render(const OFX::RenderArguments &args)
{
  if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    OFX::throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
  }

  // instantiate the render code based on the pixel depth of the dst clip
  OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
  OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

  // do the rendering
  if(dstComponents == OFX::ePixelComponentRGBA) {
    switch(dstBitDepth) {
      case OFX::eBitDepthUByte : {      
        ImageCopier<unsigned char, 4> fred(*this);
        setupAndProcess(fred, args);
      }
        break;

      case OFX::eBitDepthUShort : {
        ImageCopier<unsigned short, 4> fred(*this);
        setupAndProcess(fred, args);
      }                          
        break;

      case OFX::eBitDepthFloat : {
        ImageCopier<float, 4> fred(*this);
        setupAndProcess(fred, args);
      }
        break;
      default :
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
  }
  else {
    switch(dstBitDepth) {
      case OFX::eBitDepthUByte : {
        ImageCopier<unsigned char, 1> fred(*this);
        setupAndProcess(fred, args);
      }
        break;

      case OFX::eBitDepthUShort : {
        ImageCopier<unsigned short, 1> fred(*this);
        setupAndProcess(fred, args);
      }                          
        break;

      case OFX::eBitDepthFloat : {
        ImageCopier<float, 1> fred(*this);
        setupAndProcess(fred, args);
      }                          
        break;
      default :
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
  } 
}

mDeclarePluginFactory(OneViewPluginFactory, ;, {});

using namespace OFX;
void OneViewPluginFactory::load()
{
    // we can't be used on hosts that don't support the stereoscopic suite
    if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
        throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    }
}

void OneViewPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  // basic labels
  desc.setLabels("OneViewOFX", "OneViewOFX", "OneViewOFX");
  desc.setPluginGrouping("Views");
  desc.setPluginDescription("Takes one view from the input.");

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

void OneViewPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
  // Source clip only in the filter context
  // create the mandated source clip
  ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
  srcClip->addSupportedComponent(ePixelComponentRGBA);
  srcClip->addSupportedComponent(ePixelComponentAlpha);
  srcClip->setTemporalClipAccess(false);
  srcClip->setSupportsTiles(true);
  srcClip->setIsMask(false);

  // create the mandated output clip
  ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(ePixelComponentRGBA);
  dstClip->addSupportedComponent(ePixelComponentAlpha);
  dstClip->setSupportsTiles(true);

  // make some pages and to things in 
  PageParamDescriptor *page = desc.definePageParam("Controls");

  ChoiceParamDescriptor *view = desc.defineChoiceParam("view");
  view->setLabels("view", "view", "view");
  view->setScriptName("view");
  view->setHint("View to take from the input");
  view->appendOption("Left", "Left");
  view->appendOption("Right", "Right");
  view->setDefault(0);
  view->setAnimates(false);

  page->addChild(*view);
}

OFX::ImageEffect* OneViewPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
  return new OneViewPlugin(handle);
}

namespace OFX 
{
  namespace Plugin 
  {  
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static OneViewPluginFactory p("net.sf.openfx:oneViewPlugin", 1, 0);
      ids.push_back(&p);
    }
  }
}
