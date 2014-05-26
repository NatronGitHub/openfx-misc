/*
 Small utility to draw text using OpenGL.
 This code is based on the free glut source code.
 
 Copyright (C) 2014 INRIA
 
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
 
 
 * The freeglut library private include file.
 *
 * Copyright (c) 1999-2000 Pawel W. Olszta. All Rights Reserved.
 * Written by Pawel W. Olszta, <olszta@sourceforge.net>
 * Creation date: Thu Dec 2 1999
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PAWEL W. OLSZTA BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#include "ofxsOGLTextRenderer.h"
#include "ofxsOGLFontUtils.h"

#include <cstdlib>

namespace  {
    const SFG_Font* getFont(TextRenderer::Font font)
    {
        switch (font) {
            case TextRenderer::FONT_FIXED_8_X_13:
                return &fgFontFixed8x13;
                break;
            case TextRenderer::FONT_FIXED_9_X_15:
                return &fgFontFixed9x15;
                break;
            case TextRenderer::FONT_HELVETICA_10:
                return &fgFontHelvetica10;
                break;
            case TextRenderer::FONT_HELVETICA_12:
                return &fgFontHelvetica12;
                break;
            case TextRenderer::FONT_HELVETICA_18:
                return &fgFontHelvetica18;
                break;
            case TextRenderer::FONT_TIMES_ROMAN_10:
                return &fgFontTimesRoman10;
                break;
            case TextRenderer::FONT_TIMES_ROMAN_24:
                return &fgFontTimesRoman24;
                break;
            default:
                return (const SFG_Font*)NULL;
                break;
        }
    }
}

namespace TextRenderer {

    void  bitmapString(const char *string,TextRenderer::Font f)
    {
        unsigned char c;
        float x = 0.0f ;
        const SFG_Font* font = getFont(f);
        if (!font) {
            return;
        }
        if ( !string || ! *string )
            return;
        
        glPushClientAttrib( GL_CLIENT_PIXEL_STORE_BIT );
        glPixelStorei( GL_UNPACK_SWAP_BYTES,  GL_FALSE );
        glPixelStorei( GL_UNPACK_LSB_FIRST,   GL_FALSE );
        glPixelStorei( GL_UNPACK_ROW_LENGTH,  0        );
        glPixelStorei( GL_UNPACK_SKIP_ROWS,   0        );
        glPixelStorei( GL_UNPACK_SKIP_PIXELS, 0        );
        glPixelStorei( GL_UNPACK_ALIGNMENT,   1        );
        
        /*
         * Step through the string, drawing each character.
         * A newline will simply translate the next character's insertion
         * point back to the start of the line and down one line.
         */
        while( ( c = *string++) )
            if( c == '\n' )
            {
                glBitmap ( 0, 0, 0, 0, -x, (float) -font->Height, NULL );
                x = 0.0f;
            }
            else  /* Not an EOL, draw the bitmap character */
            {
                const GLubyte* face = font->Characters[ c ];
                
                glBitmap(
                         face[ 0 ], font->Height,     /* Bitmap's width and height    */
                         font->xorig, font->yorig,    /* The origin in the font glyph */
                         ( float )( face[ 0 ] ), 0.0, /* The raster advance; inc. x,y */
                         ( face + 1 )                 /* The packed bitmap data...    */
                         );
                
                x += ( float )( face[ 0 ] );
            }
        
        glPopClientAttrib( );
    }

    
void bitmapString(double x,double y,const char*string,TextRenderer::Font font)
{
    glPushMatrix();
    glRasterPos2f(x, y);
    bitmapString(string);
    glPopMatrix();
}

}