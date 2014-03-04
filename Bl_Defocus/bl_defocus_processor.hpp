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

#ifndef OFX_BL_DEFOCUS_PROCESSOR_HPP
#define	OFX_BL_DEFOCUS_PROCESSOR_HPP

#include"ofxsProcessing.H"

#include<cassert>

#include"CMP_defocus.h"

class bl_defocus_processor : public OFX::ImageProcessor
{
public:

    bl_defocus_processor(OFX::ImageEffect& effect);
    ~bl_defocus_processor();

    void setSrcImg(OFX::Image *img)    { _srcImg = img;}
    void setDepthImg(OFX::Image *img)  { _zImg = img;}

    void setMaskImg(OFX::Image *img, int useChannel)
    {
        assert(useChannel == 1 || useChannel == 2);
        _mskImg = img;
        _mskUseChannel = useChannel;
    }

    void setZImg(OFX::Image *img, int useChannel)
    {
        _zImg = img;
        _zUseChannel = useChannel;
    }

    void setNodeInfo(NodeDefocus *nqd) { _nodeInfo = nqd;}

    void setCameraInfo(CameraInfo *cinfo) { _camInfo = cinfo;}
    
    virtual void preProcess(void);
    virtual void multiThreadProcessImages(OfxRectI window);
    virtual void postProcess(void);

    virtual void process(void);

private:

    void copyLumToZBuffer(OFX::Image *src);
    void copyAlphaToZBuffer(OFX::Image *src);
    void clampZBuffer();

    OFX::Image *_srcImg;

    OFX::Image *_zImg;
    int _zUseChannel;

    OFX::Image *_mskImg;
    int _mskUseChannel;

    NodeDefocus *_nodeInfo;
    CameraInfo *_camInfo;

    // buffers
    CompBuf *_srcBuf;
    CompBuf *_dstBuf;
    CompBuf *_zBuf;
    CompBuf *_cradBuf;
    CompBuf *_wtsBuf;
};

#endif
