/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2016 INRIA
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
 * OFX Shadertoy plugin.
 */

#include "Shadertoy.h"

#include <string>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <algorithm>
#include <iostream>

using namespace std;

static inline
bool
isspace(char c)
{
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

void
getChannelInfo(const char* fragmentShader, int channel, std::string& label, std::string& hint)
{
    char iChannelX[14] = "// iChannelX:"; // index 11 holds the channel character
    assert(channel < 10 && iChannelX[11] == 'X');
    iChannelX[11] = '0' + channel;
    const char* tok = iChannelX;
    const char* tokpos = strstr(fragmentShader, tok);
    if (tokpos == NULL) {
        label.clear();
        hint.clear();

        return;
    }

    //printf("found label!\n");
    const char* sstart = tokpos + strlen(tok);
    // remove spaces from start
    while (isspace(*sstart)) {
        ++sstart;
    }
    const char* send = sstart;
    while (*send && *send != '(' && *send != '\n') {
        ++send;
    }
    const char* hintstart = NULL;
    if (*send == '(') {
        hintstart = send + 1;
    }
    // remove spaces from end
    if (send > sstart) {
        --send;
    }
    while (send >= sstart && isspace(*send)) {
        --send;
    }
    ++send;
    if (send > sstart) {
        label = std::string(sstart, send);
    } else {
        label.clear();
    }
    if (hintstart == NULL || !*hintstart) {
        hint.clear();

        return;
    }

    //printf("found hint!\n");
    sstart = hintstart;
    // remove spaces from start
    while (*sstart && isspace(*sstart)) {
        ++sstart;
    }
    send = sstart;
    while (*send && *send != ')' && *send != '\n') {
        ++send;
    }
    if (*send == ')') {
        --send;
    }

    // remove spaces from end
    while (send >= sstart && isspace(*send)) {
        --send;
    }
    ++send;
    if (send > sstart) {
        hint = std::string(sstart, send);
    } else {
        hint.clear();
    }
}

void
getExtraParameterInfo(const char* fragmentShader, ShadertoyPlugin::ExtraParameter &p)
{
    std::string startstr = std::string("uniform ") + ShadertoyPlugin::mapUniformTypeToStr( p.getType() ) + ' ' + p.getName();
    const char* tok = startstr.c_str();
    const char* tokpos = strstr(fragmentShader, tok);
    if (tokpos == NULL) {
        return;
    }

    //printf("found uniform!\n");
    const char* sstart = tokpos + strlen(tok);
    // look for ';' before EOF
    while (*sstart && *sstart != '\n' && *sstart != ';') {
        ++sstart;
    }
    if (!*sstart || *sstart == '\n') {
        //printf("did not find ';'!\n");
        return;
    }
    // skip spaces
    ++sstart;
    while ( *sstart && isspace(*sstart) ) {
        ++sstart;
    }
    // are we at a comment? if not, then there is nothing here
    if (!sstart[0] || sstart[0] != '/' || !sstart[1] || sstart[1] != '/') {
        //printf("did not find comment!\n");
        return;
    }
    // skip the slashes
    ++sstart;
    ++sstart;
    // skip the spaces
    while ( *sstart && isspace(*sstart) ) {
        ++sstart;
    }
    // find a '(', a ',' or a newline, which marks the end of the label
    const char* send = sstart;
    while (*send && *send != '(' && *send != ',' && *send != '\n') {
        ++send;
    }
    const char* hintstart = NULL;
    if (*send == '(') {
        hintstart = send + 1;
    }
    const char* valstart = NULL;
    if (*send == ',') {
        valstart = send + 1;
    }

    // remove spaces from end
    if (send > sstart) {
        --send;
    }
    while (send >= sstart && isspace(*send)) {
        --send;
    }
    ++send;
    if (send > sstart) {
        p.setLabel( std::string(sstart, send) );
    }
    if (hintstart != NULL) {
        //printf("found hint!\n");
        sstart = hintstart;
        // remove spaces from start
        while (*sstart && isspace(*sstart)) {
            ++sstart;
        }
        send = sstart;
        while (*send && *send != ')' && *send != '\n') {
            ++send;
        }
        // we tolerate either space or comma after closing paren
        if (*send == ')' && (send[1] == ',' || send[1] == ' ')) {
            valstart = send + 2;
        }
        if (*send == ')') {
            --send;
        }

        // remove spaces from end
        while (send >= sstart && isspace(*send)) {
            --send;
        }
        ++send;
        if (send > sstart) {
            p.setHint( std::string(sstart, send) );
        }
    }
    while (valstart != NULL) {
        //printf("found values!\n");
        bool ismax = false;
        sstart = valstart;
        // remove spaces from start
        while (*sstart && isspace(*sstart)) {
            ++sstart;
        }
        if ((*sstart == 'm' && sstart[1] == 'i' && sstart[2] == 'n') || (*sstart == 'm' && sstart[1] == 'a' && sstart[2] == 'x')) {
            ismax = (*sstart == 'm' && sstart[1] == 'a' && sstart[2] == 'x');
            sstart += 3;
            while (*sstart && isspace(*sstart)) {
                ++sstart;
            }
            if (*sstart == '=') {
                // parse value
                ++sstart;
                std::vector<double> values;
                while (*sstart && isspace(*sstart)) {
                    ++sstart;
                }
                bool paren = false;
                if (*sstart == '(') {
                    ++sstart;
                    paren = true;
                    while (*sstart && isspace(*sstart)) {
                        ++sstart;
                    }
                }
                valstart = sstart;
                //printf("*sstart1=%c\n", *sstart);
                while (valstart && *valstart) {
                    // we are at the start of a value. look for the next comma or paren
                    sstart = valstart;
                    //printf("*sstart2=%c\n", *sstart);
                    send = sstart;
                    while (*send && *send != ',' && (!paren || *send != ')') && *send != '\n') {
                        ++send;
                    }
                    if (paren && *send == ',') {
                        // next value;
                        //printf("found next value\n");
                        valstart = send + 1;
                    } else {
                        if (paren && *send == ')') {
                            ++send;
                        }
                        // no more values
                        valstart = NULL;
                    }
                    std::string a(sstart, send);
                    double v = std::atof( a.c_str() );
                    //printf("value=%g\n", v);
                    values.push_back(v);
                }
                //printf("setting\n");
                ShadertoyPlugin::ExtraParameter::ExtraParameterValue& v = ismax ? p.getMax() : p.getMin();
                switch (p.getType()) {
                    case ShadertoyPlugin::eUniformTypeNone:
                        assert(false);
                        break;
                    case ShadertoyPlugin::eUniformTypeBool:
                        assert(false);
                        break;
                    case ShadertoyPlugin::eUniformTypeInt:
                        if ( values.size() == 1) {
                            p.set(v, (int)values[0]);
                        }
                        break;
                    case ShadertoyPlugin::eUniformTypeFloat:
                        if ( values.size() == 1) {
                            p.set(v, (float)values[0]);
                        }
                        break;
                    case ShadertoyPlugin::eUniformTypeVec2:
                        if ( values.size() == 2) {
                            p.set(v, (float)values[0], (float)values[1]);
                        }
                        break;
                    case ShadertoyPlugin::eUniformTypeVec3:
                        if ( values.size() == 3) {
                            p.set(v, (float)values[0], (float)values[1], (float)values[2]);
                        }
                        break;
                    case ShadertoyPlugin::eUniformTypeVec4:
                        if ( values.size() == 4) {
                            p.set(v, (float)values[0], (float)values[1], (float)values[2], (float)values[3]);
                        }
                        break;
                }
                // look for ','
                sstart = send;
                while (*sstart && isspace(*sstart)) {
                    ++sstart;
                }
                if (*sstart == ',' && sstart[1]) {
                    //printf("found next value\n");
                    valstart = sstart + 1;
                }
            }
        }
    }
}

#ifdef MAIN

/*
 clang++ -g -fsanitize=address -I../openfx/Support/include -I../openfx/include -I../SupportExt -DOFX_SUPPORTS_OPENGLRENDER -DMAIN ShadertoyParse.cpp -o ShadertoyParse
 */


const char* s1 =
"// A shader better than any other\n"
"// iChannel0: ChannelLabel (Channel hint.)\n"
"uniform vec2 blurSize = (5., 5.); // Blur Size (The blur size in pixels.), min=(0.1,1.2), max=(1000.,1000.)\n"
"uniform float value = 2.; // ValueLabel (Value hint.) max=10, min=-10\n";


int main(int argc, char **argv)
{
    std::string label;
    std::string hint;

    for (int i = 0; i < SHADERTOY_NBINPUTS; ++i) {
        getChannelInfo(s1, i, label, hint);
        std::cout << "channel " << i << ":\n";
        std::cout << "label: '" << label << "'\n";
        std::cout << "hint: '" << hint << "'\n";
    }
    ShadertoyPlugin::ExtraParameter p;
    p.init(ShadertoyPlugin::eUniformTypeVec2, "blurSize");
    getExtraParameterInfo(s1, p);
    std::cout << "uniform " << p.getName() << ": type='" << ShadertoyPlugin::mapUniformTypeToStr(p.getType()) << " label='"<< p.getLabel() <<"', hint='" << p.getHint() << "', min=(" << p.getMin().f[0] << ',' << p.getMin().f[1] << "), max=(" << p.getMax().f[0] << ',' << p.getMax().f[1] << ")\n";
    p.init(ShadertoyPlugin::eUniformTypeFloat, "value");
    getExtraParameterInfo(s1, p);
    std::cout << "uniform " << p.getName() << ": type='" << ShadertoyPlugin::mapUniformTypeToStr(p.getType()) << " label='"<< p.getLabel() <<"', hint='" << p.getHint() << "', min=" << p.getMin().f[0] << ", max=" << p.getMax().f[0] << "\n";
}
#endif

