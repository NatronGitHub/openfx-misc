/*
 GmicGimpParser.
 
 This parser will extract all data structures expressed via the gmic_def.gmic file
 that correspond to the definition of gimp filters.
 
 Copyright (C) 2014 INRIA
 
 Author: Alexandre Gauthier-Foichat <alexandre.gauthier-foichat@inria.fr>

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
 */


#ifndef __Misc__GmicGimpParser__
#define __Misc__GmicGimpParser__

#include <string>
#include "CImg.h"

class GmicGimpParser
{
    
public:
    
    /**
     * @brief Ctor, doesn't do much more than initializing data structures
     **/
    GmicGimpParser();
    
    ~GmicGimpParser();
    
    void downloadFilters(cimg_library::CImgList<char>& sources,cimg_library::CImgList<char>& invalid_servers);
    
    /**
     * @brief Parses the given gmic definition file, an exception is thrown upon error.
     **/
    void parse(const bool tryNetUpdate,const char* locale = "en\0");
    
private:
    
    virtual unsigned int get_verbosity_mode(const bool normalized = true) { return 2; }
    
    virtual void initProgress(const std::string& /*message*/) {}
    
    virtual void progressSetText(const std::string& /*message*/) {}
    
    struct GmicGimpParserPrivate;
    GmicGimpParserPrivate* _imp;
    
};


#endif /* defined(__Misc__GmicGimpParser__) */
