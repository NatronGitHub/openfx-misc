/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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
 * OFX TestOpenGL plugin.
 */

#ifndef Misc_TestOpenGL_h
#define Misc_TestOpenGL_h

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"

void getTestOpenGLPluginID(OFX::PluginFactoryArray &ids);

/** @brief The plugin that does our work */
class TestOpenGLPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    TestOpenGLPlugin(OfxImageEffectHandle handle);

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    void renderGL(const OFX::RenderArguments &args);
    void renderMesa(const OFX::RenderArguments &args);

    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;

    OFX::DoubleParam *_scale;
    OFX::DoubleParam *_sourceScale;
    OFX::DoubleParam *_angleX;
    OFX::DoubleParam *_angleY;
    OFX::DoubleParam *_angleZ;
    OFX::BooleanParam *_useGPUIfAvailable;
};

#endif // Misc_TestOpenGL_h
