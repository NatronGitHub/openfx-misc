/**
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include "bl_zdefocus_plugin.hpp"

#include <cstring>

#include <memory>

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "bl_defocus_processor.hpp"

class bl_zdefocus_plugin : public OFX::ImageEffect
{
public:

    bl_zdefocus_plugin(OfxImageEffectHandle handle) : ImageEffect(handle)
    {
        // clips
        src_clip_   = fetchClip("Source");
        depth_clip_ = fetchClip("Depth");
        dst_clip_   = fetchClip("Output");

        quality_ = fetchChoiceParam("quality");
        clens_   = fetchDoubleParam("clens");
        fstop_   = fetchDoubleParam("fstop");
        fdist_   = fetchDoubleParam("fdist");
        btheresh_= fetchDoubleParam("thereshold");
        shape_   = fetchChoiceParam("shape");
        rotate_  = fetchDoubleParam("rotate");
        depth_   = fetchChoiceParam("depth");
    }

    void getClipPreferences(OFX::ClipPreferencesSetter& clipPreferences)
    {
        clipPreferences.setOutputFrameVarying(true);
    }

    bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
    {
        rod = src_clip_->getRegionOfDefinition(args.time);
        return true;
    }

    virtual void render(const OFX::RenderArguments &args)
    {
        bl_defocus_processor processor(*this);

        std::auto_ptr<OFX::Image> src(src_clip_->fetchImage(args.time));
        std::auto_ptr<OFX::Image> dst(dst_clip_->fetchImage(args.time));
        std::auto_ptr<OFX::Image> depth(depth_clip_->fetchImage(args.time));

        double fstop; fstop_->getValue(fstop);

        if (fstop == 128.0) {
            // nothing to do, copy src to dst and return
            int y1 = src->getBounds().y1;
            int y2 = src->getBounds().y2;
            int x1 = src->getBounds().x1;
            int x2 = src->getBounds().x2;

            for (int y = y1; y < y2; ++y) {
                memcpy(dst->getPixelAddress(x1, y), src->getPixelAddress(x1, y), (x2 - x1) * 4 * sizeof(float));
            }

            return;
        }

        processor.setSrcImg(src.get());
        processor.setDstImg(dst.get());

        int use_depth; depth_->getValue(use_depth);
        processor.setZImg(depth.get(), use_depth);

        double btheresh; btheresh_->getValue(btheresh);
        int shape; shape_->getValue(shape);
        double rot; rotate_->getValue(rot);
        int quality; quality_->getValue(quality);

        NodeDefocus node_params;
        node_params.bktype = (shape != 0) ? shape + 2 : 0;
        node_params.bthresh = btheresh;
        node_params.fstop = fstop;
        node_params.gamco = 0;
        node_params.maxblur = 0;
        node_params.no_zbuf = 0;
        node_params.rotation = rot;
        node_params.scale = 1;
        node_params.samples = 16;

        switch(quality) {
            case 0:
                node_params.preview = 1;
                break;

            case 1:
                node_params.preview = 1;
                node_params.samples = 32;
                break;

            case 2:
                node_params.preview = 1;
                node_params.samples = 64;
                break;

            case 3:
                node_params.preview = 1;
                node_params.samples = 128;
                break;

            case 4:
                node_params.preview = 0;
                break;
        }

        processor.setNodeInfo(&node_params);

        double clens; clens_->getValue(clens);
        double fdist; fdist_->getValue(fdist);

        CameraInfo cinfo;
        cinfo.lens = clens;
        cinfo.fdist = fdist;
        processor.setCameraInfo(&cinfo);

        processor.setRenderWindow(args.renderWindow);
        processor.process();
    }

private:

    // clips
    OFX::Clip *src_clip_;
    OFX::Clip *depth_clip_;
    OFX::Clip *dst_clip_;

    // params
    OFX::ChoiceParam *quality_;
    OFX::DoubleParam *clens_;
    OFX::DoubleParam *fstop_;
    OFX::DoubleParam *fdist_;
    OFX::DoubleParam *btheresh_;
    OFX::ChoiceParam *shape_;
    OFX::DoubleParam *rotate_;
    OFX::ChoiceParam *depth_;
};

void bl_zdefocus_plugin_factory::describe(OFX::ImageEffectDescriptor& desc)
{
    desc.setLabels("Bl_ZDefocus", "Bl_ZDefocus", "Bl_ZDefocus");
    desc.setPluginGrouping("Filter");

    desc.addSupportedContext(OFX::eContextGeneral);

    desc.setSupportsTiles(false);
    desc.setSupportsMultiResolution(true);
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
}

void bl_zdefocus_plugin_factory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    OFX::ClipDescriptor *srcClip = desc.defineClip("Source");
    srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(false);
    srcClip->setIsMask(false);

    OFX::ClipDescriptor *depthClip = desc.defineClip("Depth");
    depthClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    depthClip->setTemporalClipAccess(false);
    depthClip->setSupportsTiles(false);
    depthClip->setIsMask(false);
    depthClip->setOptional(false);

    OFX::ClipDescriptor *dstClip = desc.defineClip("Output");
    dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    dstClip->setSupportsTiles(false);

    // create the params
    OFX::PageParamDescriptor *page = desc.definePageParam("controls");

    OFX::ChoiceParamDescriptor *quality = desc.defineChoiceParam("quality");
    quality->setLabels("Quality", "Quality", "Quality");
    quality->setScriptName("quality");
    quality->appendOption("Preview");
    quality->appendOption("Low");
    quality->appendOption("Medium");
    quality->appendOption("High");
    quality->appendOption("Full");
    quality->setDefault(0);
    page->addChild(*quality);

    OFX::DoubleParamDescriptor *clens = desc.defineDoubleParam("clens");
    clens->setLabels("Camera Lens", "Camera Lens", "Camera Lens");
    clens->setScriptName("clens");
    clens->setRange(1, 250);
    clens->setDefault(35);
    clens->setIncrement(0.5);
    page->addChild(*clens);

    OFX::DoubleParamDescriptor *fstop = desc.defineDoubleParam("fstop");
    fstop->setLabels("FStop", "FStop", "FStop");
    fstop->setScriptName("fstop");
    fstop->setRange(0.5, 128);
    fstop->setDefault(128);
    fstop->setIncrement(0.5);
    page->addChild(*fstop);

    OFX::DoubleParamDescriptor *fdist = desc.defineDoubleParam("fdist");
    fdist->setLabels("Focal Dist", "Focal Dist", "Focal Dist");
    fdist->setScriptName("fdist");
    fdist->setRange(0, 5000);
    fdist->setDefault(0);
    fdist->setIncrement(1);
    page->addChild(*fdist);

    OFX::DoubleParamDescriptor *btheresh = desc.defineDoubleParam("thereshold");
    btheresh->setLabels("Thereshold", "Thereshold", "Thereshold");
    btheresh->setScriptName("thereshold");
    btheresh->setRange(0, 100);
    btheresh->setDefault(1);
    btheresh->setIncrement(0.1);
    page->addChild(*btheresh);

    OFX::ChoiceParamDescriptor *shape = desc.defineChoiceParam("shape");
    shape->setLabels("Shape", "Shape", "Shape");
    shape->setScriptName("shape");
    shape->appendOption("Disk");
    shape->appendOption("Triangle");
    shape->appendOption("Square");
    shape->appendOption("Pentagon");
    shape->appendOption("Hexagon");
    shape->appendOption("Heptagon");
    shape->appendOption("Octagon");
    shape->setDefault(0);
    page->addChild(*shape);

    OFX::DoubleParamDescriptor *rotate = desc.defineDoubleParam("rotate");
    rotate->setLabels("Rotate", "Rotate", "Rotate");
    rotate->setScriptName("rotate");
    rotate->setRange(0, 90);
    rotate->setDefault(0);
    rotate->setIncrement(0.5);
    page->addChild(*rotate);

    OFX::ChoiceParamDescriptor *depth = desc.defineChoiceParam("depth");
    depth->setLabels("Depth", "Deph", "Depth");
    depth->setScriptName("depth");
    depth->appendOption("Luminance");
    depth->appendOption("Alpha");
    page->addChild(*depth);
}

OFX::ImageEffect* bl_zdefocus_plugin_factory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new bl_zdefocus_plugin(handle);
}
