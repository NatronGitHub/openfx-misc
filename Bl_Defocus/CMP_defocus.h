/**
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
 * The Original Code is Copyright (C) 2006 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 */
// Modified by Esteban Tovagliari.

#ifndef CMP_DEFOCUS_H
#define CMP_DEFOCUS_H

#ifdef __cplusplus
extern "C"
{
#endif

/* defines also used for pixel size */
#define CB_RGBA		4
#define CB_VEC4		4
#define CB_VEC3		3
#define CB_VEC2		2
#define CB_VAL		1

/* defines for RGBA channels */
#define CHAN_R	0
#define CHAN_G	1
#define CHAN_B	2
#define CHAN_A	3

typedef struct rcti
{
    int xmin, xmax;
    int ymin, ymax;
} rcti;

typedef struct CompBuf
{
    float *rect;
    int x, y, xrad, yrad;
    short type, malloc;
    rcti disprect;		/* cropped part of image */
    int xof, yof;		/* relative to center of target image */

    void (*rect_procedural)(struct CompBuf *, float *, float, float);
    float procedural_size[3], procedural_offset[3];
    int procedural_type;

    void *node;
    //bNode *node;		/* only in use for procedural bufs */

    struct CompBuf *next, *prev;	/* for pass-on, works nicer than reference counting */
} CompBuf;

typedef struct NodeDefocus
{
    char bktype, rotation, preview, gamco;
    short samples, no_zbuf;
    float fstop, maxblur, bthresh, scale;
} NodeDefocus;

typedef struct CameraInfo
{
    float lens;
    float fdist;
} CameraInfo;

// prototypes

CompBuf *alloc_compbuf(int sizex, int sizey, int type, int alloc);
void free_compbuf(CompBuf *cbuf);

void defocus_blur_preprocess(NodeDefocus *nqd, CompBuf *result, CompBuf *img, CompBuf *zbuf, CompBuf *crad, CompBuf *wts, 
                                float inpval, int no_zbuf, CameraInfo *cinfo);

void defocus_blur(int y0, int y1, NodeDefocus *nqd, CompBuf *result, CompBuf *img, CompBuf *zbuf, CompBuf *crad, CompBuf *wts,
                    float inpval, int no_zbuf, CameraInfo *cinfo);

#ifdef __cplusplus
}
#endif

#endif
