/*
OFX RGBLut plugin, a plugin that illustrates the use of the OFX Support library.

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
*/

#ifdef _WINDOWS
#include <windows.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <stdio.h>
#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"

class RGBLutBase : public OFX::ImageProcessor {
protected :
  OFX::Image *_srcImg;
public :
  RGBLutBase(OFX::ImageEffect &instance): OFX::ImageProcessor(instance), _srcImg(0)
  {        
  }
  void setSrcImg(OFX::Image *v) {_srcImg = v;}
};

template <class PIX, int nComponents, int max>
class ImageRGBLutProcessor : public RGBLutBase 
{
protected:
    PIX _lookupTable[3][max];
public :
  ImageRGBLutProcessor(OFX::ImageEffect &instance, const OFX::RenderArguments &args)
    : RGBLutBase(instance) {
        // build the LUT
        OFX::ParametricParam  *lookupTable = instance.fetchParametricParam("lookupTable");
        for(int component = 0; component < 3; ++component) {
            for(int position = 0; position < max; ++position) {
                // position to evaluate the param at
                float parametricPos = float(position)/(max-1);

                // evaluate the parametric param
                double value = lookupTable->getValue(component, args.time, parametricPos);

                // set that in the lut
                _lookupTable[component][position] = std::max(PIX(0),std::min(PIX(value*(max-1)+0.5), PIX(max-1)));
            }
        }
  }
  void multiThreadProcessImages(OfxRectI procWindow)
  {
    for(int y = procWindow.y1; y < procWindow.y2; y++) 
    {
      if(_effect.abort()) 
        break;
      PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
      for(int x = procWindow.x1; x < procWindow.x2; x++) 
      {
        PIX *srcPix = (PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
        if(srcPix)
        {
          for(int c = 0; c < nComponents; c++)
            dstPix[c] = max - srcPix[c];
        }
        else 
        {
          for(int c = 0; c < nComponents; c++)
            dstPix[c] = 0;
        }
        dstPix += nComponents;
      }
    }
  }
};

template <int nComponents, int max>
class ImageRGBLutProcessorFloat : public RGBLutBase
{
protected:
    typedef float PIX;
    PIX _lookupTable[3][max];
public :
  ImageRGBLutProcessorFloat(OFX::ImageEffect &instance, const OFX::RenderArguments &args)
    : RGBLutBase(instance)
  {
    // build the LUT
    OFX::ParametricParam  *lookupTable = instance.fetchParametricParam("lookupTable");
    for(int component = 0; component < 3; ++component) {
      for(int position = 0; position < max; ++position) {
        // position to evaluate the param at
        double parametricPos = float(position)/(max-1);

        // evaluate the parametric param
        double value = lookupTable->getValue(component, args.time, parametricPos);
        //value = value * (max-1);
        //value = clamp(value, 0, max-1);

        // set that in the lut
        _lookupTable[component][position] = (PIX)value;
      }
    }
  }
  float interpolate(int component, float value) {
      if (component == 3) { // alpha
          return value;
      }
      if (value < 0.) {
          return _lookupTable[component][0];
      } else if (value >= 1.) {
          return _lookupTable[component][max-1];
      } else {
          int i = (int)(value * (max-1));
          assert(i < max-1);
          float alpha = value - (float)i / (max-1);
          return _lookupTable[component][i] * (1.-alpha) + _lookupTable[component][i] * alpha;
      }
  }
  void multiThreadProcessImages(OfxRectI procWindow)
  {
    for(int y = procWindow.y1; y < procWindow.y2; y++) 
    {
      if(_effect.abort()) 
        break;
      PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
      for(int x = procWindow.x1; x < procWindow.x2; x++) 
      {
        PIX *srcPix = (PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
        if(srcPix) 
        {
          for(int c = 0; c < nComponents; c++)
            dstPix[c] = 1. - srcPix[c];
        }
        else 
        {
          for(int c = 0; c < nComponents; c++)
            dstPix[c] = 0;
        }
        dstPix += nComponents;
      }
    }
  }
};



template<class TypeCarrier, int kComponents, int kMax>
class Analyser
{
public:
  Analyser(OFX::Clip* srcClip, OFX::DoubleParam* dbl)
  {
    OfxRangeD range = srcClip->getFrameRange();
    for(double d = range.min; d< range.max; ++d)
    {
      std::auto_ptr<OFX::Image> src(srcClip->fetchImage(d));
      dbl->setValueAtTime(d, d);
    }
  }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RGBLutPlugin : public OFX::ImageEffect 
{
protected :
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;

public :
  RGBLutPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
  {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
  }

  virtual void render(const OFX::RenderArguments &args);
  void setupAndProcess(RGBLutBase &, const OFX::RenderArguments &args);
};


void RGBLutPlugin::setupAndProcess(RGBLutBase &processor, const OFX::RenderArguments &args)
{
  std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
  OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
  OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
  std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));

  if(src.get()) 
  {
    OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = src->getPixelComponents();

    // see if they have the same depths and bytes and all
    if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
      throw int(1); // HACK!! need to throw an sensible exception here!
  }

  processor.setDstImg(dst.get());
  processor.setSrcImg(src.get());
  processor.setRenderWindow(args.renderWindow);
  processor.process();
}


void RGBLutPlugin::render(const OFX::RenderArguments &args)
{
  OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
  OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

  if(dstComponents == OFX::ePixelComponentRGBA) 
  {
    switch(dstBitDepth) 
    {
    case OFX::eBitDepthUByte : 
      {      
        ImageRGBLutProcessor<unsigned char, 4, 255> fred(*this, args);
        setupAndProcess(fred, args);
      }
      break;

    case OFX::eBitDepthUShort : 
      {
        ImageRGBLutProcessor<unsigned short, 4, 65535> fred(*this, args);
        setupAndProcess(fred, args);
      }                          
      break;

    case OFX::eBitDepthFloat : 
      {
        ImageRGBLutProcessorFloat<4,100> fred(*this, args);
        setupAndProcess(fred, args);
      }
      break;
    }
  }
  else {
    switch(dstBitDepth) 
    {
    case OFX::eBitDepthUByte : 
      {
        ImageRGBLutProcessor<unsigned char, 1, 255> fred(*this, args);
        setupAndProcess(fred, args);
      }
      break;

    case OFX::eBitDepthUShort : 
      {
        ImageRGBLutProcessor<unsigned short, 1, 65535> fred(*this, args);
        setupAndProcess(fred, args);
      }                          
      break;

    case OFX::eBitDepthFloat : 
      {
        ImageRGBLutProcessorFloat<1,100> fred(*this, args);
        setupAndProcess(fred, args);
      }                          
      break;
    }
  } 
}


mDeclarePluginFactory(RGBLutPluginFactory, {}, {});

using namespace OFX;
void RGBLutPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  desc.setLabels("RGBLut", "RGBLut", "RGBLut");
  desc.setPluginGrouping("OFX");
  desc.addSupportedContext(eContextFilter);
  desc.addSupportedBitDepth(eBitDepthUByte);
  desc.addSupportedBitDepth(eBitDepthUShort);
  desc.addSupportedBitDepth(eBitDepthFloat);

  desc.setSingleInstance(false);
  desc.setHostFrameThreading(false);
  desc.setSupportsMultiResolution(true);
  desc.setSupportsTiles(true);
  desc.setTemporalClipAccess(false);
  desc.setRenderTwiceAlways(false);
  desc.setSupportsMultipleClipPARs(false);

  if (!OFX::getImageEffectHostDescription()->supportsParametricParameter) {
	throwHostMissingSuiteException(kOfxParametricParameterSuite);
  }
}

void RGBLutPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
  ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
  srcClip->addSupportedComponent(ePixelComponentRGBA);
  srcClip->addSupportedComponent(ePixelComponentAlpha);
  srcClip->setTemporalClipAccess(false);
  srcClip->setSupportsTiles(true);
  srcClip->setIsMask(false);

  ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(ePixelComponentRGBA);
  dstClip->addSupportedComponent(ePixelComponentAlpha);
  dstClip->setSupportsTiles(true);

  if (OFX::getImageEffectHostDescription()->supportsParametricParameter) {
    // define it
    OFX::ParametricParamDescriptor* lookupTable = desc.defineParametricParam("lookupTable");
    assert(lookupTable);
    lookupTable->setLabel("Lookup Table");
    lookupTable->setHint("Colour lookup table");
    lookupTable->setScriptName("lookupTable");

    // define it as three dimensional
    lookupTable->setDimension(3);

    // label our dimensions are r/g/b
    lookupTable->setDimensionLabel("red", 0);
    lookupTable->setDimensionLabel("green", 1);
    lookupTable->setDimensionLabel("blue", 2);

    // set the UI colour for each dimension
    const OfxRGBColourD red   = {1,0,0};		//set red color to red curve
    const OfxRGBColourD green = {0,1,0};		//set green color to green curve
    const OfxRGBColourD blue  = {0,0,1};		//set blue color to blue curve
    lookupTable->setUIColour( 0, red );
    lookupTable->setUIColour( 1, green );
    lookupTable->setUIColour( 2, blue );

    // set the min/max parametric range to 0..1
    lookupTable->setRange(0.0, 1.0);

    // set a default curve, this example sets an invert
    //OfxParamHandle descriptor;
    //gParamHost->paramGetHandle(paramSet, "lookupTable", &descriptor, NULL);
    for(int component = 0; component < 3; ++component) {
      // add a control point at 0, value is 1
      lookupTable->addControlPoint(component, // curve to set
                                   0.0,   // time, ignored in this case, as we are not adding a ket
                                   0.0,   // parametric position, zero
                                   1.0,   // value to be, 1
                                   false);   // don't add a key
      // add a control point at 1, value is 0
      lookupTable->addControlPoint(component, 0.0, 1.0, 0.0, false);
    }
  }
}

OFX::ImageEffect* RGBLutPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
  return new RGBLutPlugin(handle);
}

namespace OFX 
{
  namespace Plugin 
  {  
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static RGBLutPluginFactory p("net.sf.openfx:RGBLutPlugin", 1, 0);
      ids.push_back(&p);
    }
  }
}
