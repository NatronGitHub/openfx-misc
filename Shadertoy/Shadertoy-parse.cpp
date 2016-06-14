/*
 clang++ -g -fsanitize=address -I../openfx/Support/include -I../openfx/include -I../SupportExt -DOFX_SUPPORTS_OPENGLRENDER Shadertoy-parse.cpp -o Shadertoy-parse
 */
#include <string>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <algorithm>
#include <iostream>

#include "Shadertoy.h"

using namespace std;

const char* s1 =
"// A shader better than any other\n"
"// iChannel0: ChannelLabel (Channel hint.)\n"
"uniform vec2 blurSize = (5., 5.); // Blur Size (The blur size in pixels.) min=(0.,0.), max=(1000.,1000.)\n"
"uniform float value = 2.; // ValueLabel (Value hint.) min=0\n";

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
    const char* tokpos = strstr(s1, tok);
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
    while (*send && *send != '(') {
        ++send;
    }
    const char* hintstart = NULL;
    if (*send == '(') {
        hintstart = send + 1;
    }
    // remove spaces from end
    --send;
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
    while (isspace(*sstart)) {
        ++sstart;
    }
    send = sstart;
    while (*send && *send != ')') {
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
    return;
}


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
    std::cout << "uniform " << p.getName() << ": type='" << ShadertoyPlugin::mapUniformTypeToStr(p.getType()) << " label='"<< p.getLabel() <<"', hint='" << p.getHint() << "'\n";
    p.init(ShadertoyPlugin::eUniformTypeFloat, "value");
    getExtraParameterInfo(s1, p);
    std::cout << "uniform " << p.getName() << ": type='" << ShadertoyPlugin::mapUniformTypeToStr(p.getType()) << " label='"<< p.getLabel() <<"', hint='" << p.getHint() << "'\n";
}
