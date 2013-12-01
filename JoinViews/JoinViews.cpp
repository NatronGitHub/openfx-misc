/*
OFX JoinViews plugin.
JoinView inputs to make a stereo output.

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
template <class PIX, int nComponents, int max>
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
class JoinViewsPlugin : public OFX::ImageEffect {
protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcLeftClip_;
  OFX::Clip *srcRightClip_;

public :
  /** @brief ctor */
  JoinViewsPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcLeftClip_(0)
    , srcRightClip_(0)
  {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcLeftClip_ = fetchClip("Left");
    srcRightClip_ = fetchClip("Right");
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
JoinViewsPlugin::setupAndProcess(CopierBase &processor, const OFX::RenderArguments &args)
{
  // get a dst image
  std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
  OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
  OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

  // fetch main input image
  std::auto_ptr<OFX::Image> src(args.renderView == 0
                                ? srcLeftClip_->fetchStereoscopicImage(args.time,0)
                                : srcRightClip_->fetchStereoscopicImage(args.time,0));

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
JoinViewsPlugin::render(const OFX::RenderArguments &args)
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
        ImageCopier<unsigned char, 4, 255> fred(*this);
        setupAndProcess(fred, args);
      }
        break;

      case OFX::eBitDepthUShort : {
        ImageCopier<unsigned short, 4, 65535> fred(*this);
        setupAndProcess(fred, args);
      }                          
        break;

      case OFX::eBitDepthFloat : {
        ImageCopier<float, 4, 1> fred(*this);
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
        ImageCopier<unsigned char, 1, 255> fred(*this);
        setupAndProcess(fred, args);
      }
        break;

      case OFX::eBitDepthUShort : {
        ImageCopier<unsigned short, 1, 65535> fred(*this);
        setupAndProcess(fred, args);
      }                          
        break;

      case OFX::eBitDepthFloat : {
        ImageCopier<float, 1, 1> fred(*this);
        setupAndProcess(fred, args);
      }                          
        break;
      default :
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
  } 
}

mDeclarePluginFactory(JoinViewsPluginFactory, ;, {});

using namespace OFX;

void JoinViewsPluginFactory::load()
{
  // we can't be used on hosts that don't support the stereoscopic suite
  if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
  }
}

void JoinViewsPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  // basic labels
  desc.setLabels("JoinViewsOFX", "JoinViewsOFX", "JoinViewsOFX");
  desc.setPluginGrouping("Views");
  desc.setPluginDescription("JoinView inputs to make a stereo output. "
                            "The first view from each input is copied to the left and right views of the output.");

  // add the supported contexts, only general at the moment, because there are several inputs
  // (see http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectContextFilter)
  desc.addSupportedContext(eContextGeneral);

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

void JoinViewsPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
  // create the source clips from the rightmost one (in Nuke's GUI) to the leftmost
  ClipDescriptor *srcRightClip = desc.defineClip("Right");
  srcRightClip->addSupportedComponent(ePixelComponentRGBA);
  srcRightClip->addSupportedComponent(ePixelComponentAlpha);
  srcRightClip->setTemporalClipAccess(false);
  srcRightClip->setSupportsTiles(true);
  srcRightClip->setIsMask(false);

  ClipDescriptor *srcLeftClip = desc.defineClip("Left");
  srcLeftClip->addSupportedComponent(ePixelComponentRGBA);
  srcLeftClip->addSupportedComponent(ePixelComponentAlpha);
  srcLeftClip->setTemporalClipAccess(false);
  srcLeftClip->setSupportsTiles(true);
  srcLeftClip->setIsMask(false);

  // create the mandated output clip
  ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(ePixelComponentRGBA);
  dstClip->addSupportedComponent(ePixelComponentAlpha);
  dstClip->setSupportsTiles(true);

}

OFX::ImageEffect* JoinViewsPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
  return new JoinViewsPlugin(handle);
}

namespace OFX 
{
  namespace Plugin 
  {  
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static JoinViewsPluginFactory p("net.sf.openfx:joinViewsPlugin", 1, 0);
      ids.push_back(&p);
    }
  }
}
