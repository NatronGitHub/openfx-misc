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

#include "bl_defocus_processor.hpp"

#include <limits>
#include <algorithm>

namespace OFX
{

void makeEmpty(OfxRectI& r)
{
    r.x1 = std::numeric_limits<int>::max();
    r.y1 = std::numeric_limits<int>::max();
    r.x2 = - r.x1;
    r.y2 = - r.y1;
}

void makeEmpty(OfxRectD& r)
{
    r.x1 = std::numeric_limits<double>::max();
    r.y1 = std::numeric_limits<double>::max();
    r.x2 = - r.x1;
    r.y2 = - r.y1;
}

bool isEmpty(const OfxRectI& r) { return (r.x1 >= r.x2) || (r.y1 >= r.y2);}
bool isEmpty(const OfxRectD& r) { return (r.x1 >= r.x2) || (r.y1 >= r.y2);}

OfxRectD intersect(const OfxRectD& a, const OfxRectD& b)
{
    OfxRectD result;
    makeEmpty(result);

    if (!(a.x1 > b.x2 || a.x2 < b.x1 || a.y1 > b.y2 || a.y2 < b.y1)) {
        result.x1 = std::max(a.x1, b.x1);
        result.y1 = std::max(a.y1, b.y1);
        result.x2 = std::min(a.x2, b.x2);
        result.y2 = std::min(a.y2, b.y2);
    }

    return result;
}

OfxRectI intersect(const OfxRectI& a, const OfxRectI& b)
{
    OfxRectI result;
    makeEmpty(result);

    if (!(a.x1 > b.x2 || a.x2 < b.x1 || a.y1 > b.y2 || a.y2 < b.y1)) {
        result.x1 = std::max(a.x1, b.x1);
        result.y1 = std::max(a.y1, b.y1);
        result.x2 = std::min(a.x2, b.x2);
        result.y2 = std::min(a.y2, b.y2);
    }

    return result;
}

OfxRectD join(const OfxRectD& a, const OfxRectD& b)
{
    OfxRectD result;
    result.x1 = std::min(a.x1, b.x1);
    result.y1 = std::min(a.y1, b.y1);
    result.x2 = std::max(a.x2, b.x2);
    result.y2 = std::max(a.y2, b.y2);
    return result;
}

OfxRectI join(const OfxRectI& a, const OfxRectI& b)
{
    OfxRectI result;
    result.x1 = std::min(a.x1, b.x1);
    result.y1 = std::min(a.y1, b.y1);
    result.x2 = std::max(a.x2, b.x2);
    result.y2 = std::max(a.y2, b.y2);
    return result;
}

} // namespace OFX
bl_defocus_processor::bl_defocus_processor(OFX::ImageEffect& effect) : OFX::ImageProcessor(effect)
{
    _srcImg = 0;
    _mskImg = 0;
    _zImg = 0;

    _mskUseChannel = 0;
    _zUseChannel = 1;

    _srcBuf = 0;
    _dstBuf = 0;
    _zBuf = 0;
    _cradBuf = 0;
    _wtsBuf = 0;

    _nodeInfo = 0;
    _camInfo = 0;
}

bl_defocus_processor::~bl_defocus_processor()
{
    free_compbuf(_srcBuf);
    free_compbuf(_dstBuf);
    free_compbuf(_zBuf);
    free_compbuf(_cradBuf);
    free_compbuf(_wtsBuf);
}

void bl_defocus_processor::preProcess(void)
{
    int width  = _srcImg->getBounds().x2 - _srcImg->getBounds().x1;
    int height = _srcImg->getBounds().y2 - _srcImg->getBounds().y1;

    _srcBuf = alloc_compbuf(width, height, CB_RGBA, 1);
    _dstBuf = alloc_compbuf(width, height, CB_RGBA, 1);

    // copy src image
    float *pdst = _srcBuf->rect;

    for (int y = _srcImg->getBounds().y1; y < _srcImg->getBounds().y2; ++y) {
        float *psrc = reinterpret_cast<float*>(_srcImg->getPixelAddress(_srcImg->getBounds().x1, y));

        for (int x = _srcImg->getBounds().x1; x < _srcImg->getBounds().x2; ++x) {
            *pdst++ = *psrc++;
            *pdst++ = *psrc++;
            *pdst++ = *psrc++;
            *pdst++ = *psrc++;
        }
    }

    // mask / zbuffer
    if (_zImg) {
        _zBuf = alloc_compbuf(width, height, CB_VAL, 1);

        if (_zUseChannel) {
            copyAlphaToZBuffer(_zImg);
        } else {
            copyLumToZBuffer(_zImg);
        }
    } else {
        if (_mskUseChannel && _mskImg) {
            if (_mskUseChannel == 1) {
                copyLumToZBuffer(_mskImg);
            } else {
                copyAlphaToZBuffer(_mskImg);
            }

            clampZBuffer();
        }
    }

    _wtsBuf  = alloc_compbuf(width, height, CB_VAL, 1);
    _cradBuf = alloc_compbuf(width, height, CB_VAL, 1);

    defocus_blur_preprocess(_nodeInfo, _dstBuf, _srcBuf, _zBuf, _cradBuf, _wtsBuf, _nodeInfo->scale, _zImg == 0, _camInfo);

    // it seems the node doesn't run correctly in parallel
    // so we run the process here instead of multiThreadProcessImages
    defocus_blur(0, _dstBuf->y, _nodeInfo, _dstBuf, _srcBuf, _zBuf, _cradBuf, _wtsBuf, _nodeInfo->scale, _zImg == 0, _camInfo);
}

void bl_defocus_processor::multiThreadProcessImages(OfxRectI window)
{
    // it seems the node doesn't run correctly in parallel
    // we can't do anything here

    //    int y0 = window.y1 - _dstImg->getBounds().y1;
    //    int y1 = y0 + window.y2 - window.y1;
    //    defocus_blur(y0, y1, _nodeInfo, _dstBuf, _srcBuf, _zBuf, _cradBuf, _wtsBuf, _nodeInfo->scale, _zImg == 0);
}

void bl_defocus_processor::postProcess(void)
{
    // copy back the resulting image
    float *psrc = _dstBuf->rect;

    for (int y = _dstImg->getBounds().y1; y < _dstImg->getBounds().y2; ++y) {
        float *pdst = reinterpret_cast<float*>(_dstImg->getPixelAddress(_dstImg->getBounds().x1, y));

        for (int x = _dstImg->getBounds().x1; x < _dstImg->getBounds().x2; ++x) {
            *pdst++ = *psrc++;
            *pdst++ = *psrc++;
            *pdst++ = *psrc++;
            *pdst++ = *psrc++;
        }
    }
}

void bl_defocus_processor::process(void)
{
    // is it OK ?
    if (!_dstImg || (_renderWindow.x2 - _renderWindow.x1 == 0 && _renderWindow.y2 - _renderWindow.y1)) {
        return;
    }

    preProcess();

    if (_nodeInfo->preview) {
        multiThread(1);    // preview can run only in one processor, right?
    } else {
        multiThread();      // normal parallel code
    }

    postProcess();
}

void bl_defocus_processor::copyLumToZBuffer(OFX::Image *src)
{
    OfxRectI isect = OFX::intersect(_dstImg->getBounds(), src->getBounds());

    if (OFX::isEmpty(isect)) {
        return;
    }

    _zBuf = alloc_compbuf(_dstBuf->x, _dstBuf->y, CB_VAL, 1);

    for (int j = isect.y1; j < isect.y2; ++j) {
        float *psrc = reinterpret_cast<float*>(src->getPixelAddress(isect.x1, j));
        float *pdst = _zBuf->rect + ((j - _dstImg->getBounds().y1) * _zBuf->x) + (isect.x1 - _dstImg->getBounds().x1);

        for (int i = isect.x1; i < isect.x2; ++i) {
            // TODO: use the real luminance weights here
            float v = (psrc[0] + psrc[1] + psrc[2]) / 3.0f;
            *pdst++ = v;
            psrc += 4;
        }
    }
}

void bl_defocus_processor::copyAlphaToZBuffer(OFX::Image *src)
{
    OfxRectI isect = OFX::intersect(_dstImg->getBounds(), src->getBounds());

    if (OFX::isEmpty(isect))
        return;

    _zBuf = alloc_compbuf(_dstBuf->x, _dstBuf->y, CB_VAL, 1);

    for (int j = isect.y1; j < isect.y2; ++j) {
        float *psrc = reinterpret_cast<float*>(src->getPixelAddress(isect.x1, j));
        float *pdst = _zBuf->rect + ((j - _dstImg->getBounds().y1) * _zBuf->x) + (isect.x1 - _dstImg->getBounds().x1);

        for (int i = isect.x1; i < isect.x2; ++i) {
            *pdst++ = psrc[3];
            psrc += 4;
        }
    }
}

void bl_defocus_processor::clampZBuffer()
{
    float *p = _zBuf->rect;

    for (int j = 0; j < _zBuf->y; ++j) {
        for (int i = 0; i < _zBuf->x; ++i) {
            if (*p < 0.0f) *p = 0.0f;
            if (*p > 1.0f) *p = 1.0f;
            ++p;
        }
    }
}
