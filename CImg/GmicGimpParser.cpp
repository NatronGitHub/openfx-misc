/*
GmicGimpParser plugin.
 
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

#include <cassert>
#include <locale>
#include <algorithm>
#include <iostream>
#include "GmicGimpParser.h"
#include "CImg.h"
#include "gmic.h"


#undef _gmic_path
#if cimg_OS==2
#define _gmic_path "_gmic\\"
#define _gmic_file_prefix ""
#else
#define _gmic_path ""
#define _gmic_file_prefix "."
#endif

extern char data_gmic_def[];
extern unsigned int size_data_gmic_def;
extern unsigned char data_gmic_logo[];
extern unsigned int size_data_gmic_logo;

using namespace cimg_library;
using namespace Gmic;

namespace {
    
    // Get the folder path of configuration files.
    static const char* get_conf_path() {
        const char *path_conf = getenv("GMIC_GIMP_PATH");
        if (!path_conf) {
#if cimg_OS!=2
            path_conf = getenv("HOME");
#else
            path_conf = getenv("APPDATA");
#endif
        }
        return path_conf;
    }
    
    // Compute the basename of a URL or a regular file path.
    static const char* gmic_basename(const char *const s)  {
        const char *p = 0, *np = s;
        while (np>=s && (p=np)) np = std::strchr(np,'/') + 1;
        while (np>=s && (p=np)) np = std::strchr(np,'\\') + 1;
        return p;
    }
    
}

struct ParameterBase::ParameterBasePrivate
{
    std::string label;
    std::string scriptName;
    int nDim;
    bool silent;
    
    ParameterBasePrivate(const std::string& label,int nDim)
    : label(label)
    , scriptName(label)
    , nDim(nDim)
    , silent(false)
    {
        if (!scriptName.empty()) {
            scriptName.erase(std::remove(scriptName.begin(), scriptName.end(), ' '), scriptName.end());
            scriptName.front() = std::tolower(scriptName.front());
        }
    }
};

ParameterBase::ParameterBase(const std::string& label,int nDim)
: _imp(new ParameterBasePrivate(label,nDim))
{
    
}

ParameterBase::~ParameterBase()
{
    delete _imp;
}

const std::string&
ParameterBase::getLabel() const
{
    return _imp->label;
}

int
ParameterBase::getNDim() const
{
    return _imp->nDim;
}

const std::string&
ParameterBase::getScriptName() const
{
    return _imp->scriptName;
}


bool
ParameterBase::isSilent() const
{
    return _imp->silent;
}

void
ParameterBase::setSilent(bool silent)
{
    _imp->silent = silent;
}

struct GmicGimpParser::GmicGimpParserPrivate
{
    GmicGimpParser* publicInterface;
    int nPlugins;
    
    //The root of the plugins tree
    std::list<GmicTreeNode*> firstLevelEntries;
    
    GmicGimpParserPrivate(GmicGimpParser* publicInterface)
    : publicInterface(publicInterface)
    , nPlugins(0)
    , firstLevelEntries()
    {
        
    }
    
    void downloadFilters(cimg_library::CImgList<char>& sources,cimg_library::CImgList<char>& invalid_servers);

};

GmicGimpParser::GmicGimpParser()
: _imp(new GmicGimpParser::GmicGimpParserPrivate(this))
{
    
}

GmicGimpParser::~GmicGimpParser()
{
    reset();
    delete _imp;
}

void
GmicGimpParser::reset()
{
    _imp->nPlugins = 0;
    for (std::list<GmicTreeNode*>::iterator it = _imp->firstLevelEntries.begin(); it != _imp->firstLevelEntries.end(); ++it) {
        delete *it;
    }
    _imp->firstLevelEntries.clear();
}

const std::list<GmicTreeNode*>&
GmicGimpParser::getFirstLevelEntries() const
{
    return _imp->firstLevelEntries;
}

struct GmicTreeNode::GmicTreeNodePrivate
{
    GmicTreeNode* parent;
    std::list<GmicTreeNode*> children;
    
    std::string name;
    std::string command;
    std::string previewCommand;
    std::string arguments;
    
    double previewFactor;
    
    bool doNotRemoveFromParentChildrenOnDeletion;
    
    std::list<ParameterBase*> parameters;
    
    GmicTreeNodePrivate()
    : parent(0)
    , children()
    , name()
    , command()
    , previewCommand()
    , arguments()
    , previewFactor(1.)
    , doNotRemoveFromParentChildrenOnDeletion(false)
    , parameters()
    {
        
    }
};

GmicTreeNode::GmicTreeNode()
: _imp(new GmicTreeNodePrivate())
{
    
}

GmicTreeNode::~GmicTreeNode()
{
    if (_imp->parent && !_imp->doNotRemoveFromParentChildrenOnDeletion) {
        _imp->parent->tryRemoveChild(this);
    }
    for (std::list<ParameterBase*>::iterator it = _imp->parameters.begin(); it != _imp->parameters.end(); ++it) {
        delete *it;
    }
    
    for (std::list<GmicTreeNode*>::iterator it = _imp->children.begin(); it != _imp->children.end(); ++it) {
        (*it)->_imp->doNotRemoveFromParentChildrenOnDeletion = true;
        delete *it;
    }
    delete _imp;
}

void
GmicTreeNode::addParameterAndTakeOwnership(ParameterBase* param)
{
    _imp->parameters.push_back(param);
}

const std::list<ParameterBase*>&
GmicTreeNode::getParameters() const
{
    return _imp->parameters;
}

GmicTreeNode*
GmicTreeNode::getParent() const
{
    return _imp->parent;
}

void
GmicTreeNode::setParent(GmicTreeNode* parent)
{

    if (_imp->parent) {
        _imp->parent->tryRemoveChild(this);
    }
    _imp->parent = parent;
    if (parent) {
        parent->tryAddChild(this);
    }
}

const std::list<GmicTreeNode*>&
GmicTreeNode::getChildren() const
{
    return _imp->children;
}


bool
GmicTreeNode::tryAddChild(GmicTreeNode* child)
{
    std::list<GmicTreeNode*>::iterator found = std::find(_imp->children.begin(), _imp->children.end(), child);
    if (found == _imp->children.end()) {
        _imp->children.push_back(child);
        return true;
    }
    return false;
}

bool
GmicTreeNode::tryRemoveChild(GmicTreeNode* child)
{
    std::list<GmicTreeNode*>::iterator found = std::find(_imp->children.begin(), _imp->children.end(), child);
    if (found != _imp->children.end()) {
        _imp->children.erase(found);
        return true;
    }
    return false;
}

const std::string&
GmicTreeNode::getName() const
{
    return _imp->name;
}

void
GmicTreeNode::setName(const std::string& name)
{
    _imp->name = name;
}

const std::string&
GmicTreeNode::getGmicCommand() const
{
    return _imp->command;
}

void
GmicTreeNode::setGmicCommand(const std::string& command)
{
    _imp->command = command;
}

const std::string&
GmicTreeNode::getGmicPreviewCommand() const
{
    return _imp->previewCommand;
}

void
GmicTreeNode::setGmicPreviewCommand(const std::string& pCommand)
{
    _imp->previewCommand = pCommand;
}

const std::string&
GmicTreeNode::getGmicArguments() const
{
    return _imp->arguments;
}

void
GmicTreeNode::setGmicArguments(const std::string& args)
{
    _imp->arguments = args;
}

void
GmicTreeNode::appendGmicArguments(const std::string& args)
{
    _imp->arguments.append(args);
}

double
GmicTreeNode::getPreviewZoomFactor() const
{
    return _imp->previewFactor;
}

void
GmicTreeNode::setPreviewZoomFactor(double s)
{
    _imp->previewFactor = s;
}

void
GmicGimpParser::GmicGimpParserPrivate::downloadFilters(cimg_library::CImgList<char>& sources,cimg_library::CImgList<char>& invalid_servers)
{
    // Build list of filter sources.
    CImgList<float> _sources;
    CImgList<char> _names;
    char command[1024] = { 0 };
    CImg<char> gmic_additional_commands;

    
    cimg_snprintf(command,sizeof(command),"%s-gimp_filter_sources",
                  publicInterface->get_verbosity_mode() > 4? "-debug " : publicInterface->get_verbosity_mode() > 2 ? "" : "-v -99 ");
    
    try {
        gmic(command,_sources,_names,gmic_additional_commands,true);
    } catch (...) {
        
    }
    _sources.move_to(sources);
    cimglist_for(sources,l) {
        char &c = sources[l].unroll('x').back();
        if (c) {
            if (c == 1) {
                c = 0;
                sources[l].columns(0,sources[l].width());
                sources[l].back() = 1;
            } else {
                sources[l].columns(0,sources[l].width());
            }
        }
    }
    
    publicInterface->initProgress(" G'MIC : Update filters...");

    
    // Get filter definition files from external web servers.
    const char *const path_conf = get_conf_path(), *const path_tmp = cimg::temporary_path();
    char filename[1024] = { 0 };
    char filename_tmp[1024] = { 0 }, sep = 0;
    
    cimglist_for(sources,l) {
        if (!cimg::strncasecmp(sources[l],"http://",7) ||
                             !cimg::strncasecmp(sources[l],"https://",8)) {
            
            const char *const s_basename = gmic_basename(sources[l]);
            
            {
                std::string progressText(" G'MIC : Update filters '");
                progressText.append(s_basename);
                progressText.append("'...");
                publicInterface->progressSetText(progressText);
            }
            
            cimg_snprintf(filename_tmp,sizeof(filename_tmp),"%s%c%s%s",
                          path_tmp,cimg_file_separator,_gmic_file_prefix,s_basename);
            cimg_snprintf(filename,sizeof(filename),"%s%c%s%s",
                          path_conf,cimg_file_separator,_gmic_file_prefix,s_basename);
            std::remove(filename_tmp);
            
            // Try curl first.
            if (publicInterface->get_verbosity_mode()) { // Verbose mode.
                cimg_snprintf(command,sizeof(command),_gmic_path "curl -f --compressed -o \"%s\" %s",
                              filename_tmp,sources[l].data());
                std::fprintf(cimg::output(),"\n[gmic_gimp]./update/ %s\n",command);
                std::fflush(cimg::output());
            } else // Quiet mode.
#if cimg_OS==1
                cimg_snprintf(command,sizeof(command),_gmic_path "curl -f --silent --compressed -o \"%s\" %s 2> /dev/null",
                              filename_tmp,sources[l].data());
#else
            cimg_snprintf(command,sizeof(command),_gmic_path "curl -f --silent --compressed -o \"%s\" %s",
                          filename_tmp,sources[l].data());
#endif
            cimg::system(command);
            std::FILE *file = std::fopen(filename_tmp,"rb");
            
            // Try with 'wget' if 'curl' failed.
            if (!file) {
                if (publicInterface->get_verbosity_mode()) { // Verbose mode.
                    cimg_snprintf(command,sizeof(command),_gmic_path "wget -r -l 0 --no-cache -O \"%s\" %s",
                                  filename_tmp,sources[l].data());
                    std::fprintf(cimg::output(),"\n[gmic_gimp]./update/ %s\n",command);
                    std::fflush(cimg::output());
                } else // Quiet mode.
#if cimg_OS==1
                    cimg_snprintf(command,sizeof(command),_gmic_path "wget -q -r -l 0 --no-cache -O \"%s\" %s 2> /dev/null",
                                  filename_tmp,sources[l].data());
#else
                cimg_snprintf(command,sizeof(command),_gmic_path "wget -q -r -l 0 --no-cache -O \"%s\" %s",
                              filename_tmp,sources[l].data());
#endif
                cimg::system(command);
                file = std::fopen(filename_tmp,"rb");
            }
            
            // Download succeeded, check file content and uncompress it if necessary.
            if (file) {
                
                // Check for gzip compressed version of the file.
                if (std::fscanf(file," #@gmi%c",&sep)!=1 || sep!='c') {
                    // G'MIC header not found -> perhaps a .gz compressed file ?
                    std::fclose(file);
                    cimg_snprintf(command,sizeof(command),"%s.gz",filename_tmp);
                    std::rename(filename_tmp,command);
                    if (publicInterface->get_verbosity_mode()) { // Verbose mode.
                        cimg_snprintf(command,sizeof(command),_gmic_path "gunzip %s.gz",
                                      filename_tmp);
                        std::fprintf(cimg::output(),
                                     "\n[gmic_gimp]./update/ %s\n",
                                     command);
                        std::fflush(cimg::output());
                    } else // Quiet mode.
#if cimg_OS==1
                        cimg_snprintf(command,sizeof(command),_gmic_path "gunzip --quiet %s.gz 2> /dev/null",
                                      filename_tmp);
#else
                    cimg_snprintf(command,sizeof(command),_gmic_path "gunzip --quiet %s.gz",
                                  filename_tmp);
#endif
                    cimg::system(command);
                    file = std::fopen(filename_tmp,"rb");
                    if (!file) { // If failed, go back to initial state.
                        cimg_snprintf(command,sizeof(command),"%s.gz",filename_tmp);
                        std::rename(command,filename_tmp);
                        file = std::fopen(filename_tmp,"rb");
                    }
                }
                
                // Eventually, uncompress .cimgz file.
                if (file && (std::fscanf(file," #@gmi%c",&sep)!=1 || sep!='c')) {
                    std::rewind(file);
                    bool is_cimg = true;
                    try {
                        CImg<unsigned char> buffer;
                        buffer.load_cimg(file);
                        std::fclose(file);
                        buffer.save_raw(filename_tmp);
                        file = std::fopen(filename_tmp,"rb");
                    } catch (...) {
                        is_cimg = false;
                        std::rewind(file);
                    }
                    if (publicInterface->get_verbosity_mode()) {
                        std::fprintf(cimg::output(),
                                     "\n[gmic_gimp]./update/ File '%s' was%s in .cimg[z] format.\n",
                                     filename_tmp,is_cimg?"":" not");
                    }
                }
                
                // Copy file to its final location.
                if (file && std::fscanf(file," #@gmi%c",&sep)==1 && sep=='c') {
                    std::fclose(file);
                    CImg<unsigned char>::get_load_raw(filename_tmp).save_raw(filename);
                    std::remove(filename_tmp);
                } else {
                    invalid_servers.insert(sources[l]); // Failed in recognizing file header.
                }
                
            } else {
                invalid_servers.insert(sources[l]);  // Failed in downloading file.
            }
        }
    } // cimglist_for(sources,l) {

} // void downloadFilters(cimg_library::CImgList<char>& sources,cimg_library::CImgList<char>& invalid_servers)

void
GmicGimpParser::parse(std::string* errors,bool tryNetUpdate,const char* locale)
{
   //Reset the parser's state if it was already used
    reset();
    
    //CImgList<char> gmic_faves;                     // The list of favorites filters and their default parameters.
    CImg<char> gmic_additional_commands;           // The buffer of additional G'MIC command implementations.
    unsigned int nb_available_filters = 0;         // The number of available filters (non-testing).

    char command[1024] = { 0 };

    CImgList<char> sources;

    char filename[1024] = { 0 };
    const char *const path_conf = get_conf_path();
    
    //Initialize resources
    gmic_additional_commands.assign();
    
    if (tryNetUpdate) {
        CImgList<char> invalid_servers;
        _imp->downloadFilters(sources, invalid_servers);
        cimglist_for(sources,l) {
            const char *s_basename = gmic_basename(sources[l]);
            errors->append(s_basename);
            errors->append(": Failed to contact the server. \n");
        }
    }
    
    
    progressSetText(" G'MIC : Update filters...");
    
    // Read local source files.
    CImgList<char> _gmic_additional_commands;
    bool is_default_update = false;
    cimglist_for(sources,l) {
        const char *s_basename = gmic_basename(sources[l]);
        cimg_snprintf(filename,sizeof(filename),"%s%c%s%s",
                      path_conf,cimg_file_separator,_gmic_file_prefix,s_basename);
        const unsigned int old_exception_mode = cimg::exception_mode();
        try {
            cimg::exception_mode(0);
            CImg<char>::get_load_raw(filename).move_to(_gmic_additional_commands);
            CImg<char>::string("\n#@gimp ________\n",false).unroll('y').move_to(_gmic_additional_commands);
            if (sources[l].back()==1) is_default_update = true;
        } catch(...) {
            if (get_verbosity_mode())
                std::fprintf(cimg::output(),
                             "\n[gmic_gimp]./update/ Filter file '%s' not found.\n",
                             filename);
            std::fflush(cimg::output());
        }
        cimg::exception_mode(old_exception_mode);
 
    }
    
    if (!is_default_update) { // Add hardcoded default filters if no updates of the default commands.
        CImg<char>(data_gmic_def,1,size_data_gmic_def-1,1,1,true).move_to(_gmic_additional_commands);
        CImg<char>::string("\n#@gimp ________\n",false).unroll('y').move_to(_gmic_additional_commands);
    }
    CImg<char>::vector(0).move_to(_gmic_additional_commands);
    (_gmic_additional_commands>'y').move_to(gmic_additional_commands);
    
    // Add fave folder if necessary (make it before actually adding faves to make tree paths valids).
    CImgList<char> gmic_1stlevel_names;
    
    GmicTreeNode* parent[8] ;
    memset(parent, 0, sizeof(GmicTreeNode*) * 8);
    
    // Parse filters descriptions for GIMP, and create corresponding sorted treeview_store.
    char line[256*1024] = { 0 }, preview_command[256] = { 0 }, arguments[65536] = { 0 },
    entry[256] = { 0 }, locale_[16] = { 0 };
    std::strcpy(locale_,locale);
    int level = 0, err = 0;
    bool is_testing = false;
    nb_available_filters = 0;
    cimg_snprintf(line,sizeof(line),"#@gimp_%s ",locale);
    
    // Use English for default language if no translated filters found.
    if (!std::strstr(gmic_additional_commands,line)) {
        locale_[0] = 'e'; locale_[1] = 'n'; locale_[2] = 0;
    }
    
    GmicTreeNode* lastProcessedNode = 0;
    
    for (const char *data = gmic_additional_commands; *data; ) {
        char *_line = line;
        
        // Read new line.
        while (*data!='\n' && *data && _line<line+sizeof(line)) {
            *(_line++) = *(data++);
            *_line = 0;
        }
        
        // Skip next '\n'.
        while (*data=='\n') {
            ++data;
        }
        
        // Replace non-usual characters by spaces.
        for (_line = line; *_line; ++_line) {
            if (*_line<' ') {
                *_line = ' ';
            }
        }
        
        if (line[0]!='#' || line[1]!='@' || line[2]!='g' || // Check for a '#@gimp' line.
            line[3]!='i' || line[4]!='m' || line[5]!='p') {
            continue;
        }
        
         // Check for a localized filter.
        if (line[6]=='_') {
            
            // Weither the entry match current locale or not.
            if (line[7] == locale[0] && line[8] == locale[1] && line[9] == ' ') {
                _line = line + 10;
            } else {
                continue;
            }
            
        } else if (line[6]==' ') { // Check for a non-localized filter.
            _line = line + 7;
        } else {
            continue;
        }
        
        if (*_line!=':') { // Check for a description of a possible filter or menu folder.
            
            *entry = *command = *preview_command = *arguments = 0;
            
            err = std::sscanf(_line," %4095[^:]: %4095[^,]%*c %4095[^,]%*c %65533[^\n]",
                              entry,command,preview_command,arguments);
            
            if (err == 1) { // If entry defines a menu folder.
                
                cimg::strpare(entry,' ',false,true);
                
                char *nentry = entry;
                while (*nentry=='_') {
                    ++nentry;
                    --level;
                }
                
                //Clamp menu level to [0,7]
                if (level < 0) {
                    level = 0;
                } else if (level > 7) {
                    level = 7;
                }
                
                cimg::strpare(nentry,' ',false,true);
                cimg::strpare(nentry,'\"',true);
                
                if (*nentry) {
                    
                    std::string entryName(nentry);

                    if (level) {
                        
                        
                        GmicTreeNode* node = new GmicTreeNode();
                        node->setName(entryName);
                        GmicTreeNode* p = parent[level - 1];
                        assert(p);
                        node->setParent(p);
                        parent[level] = node;
                        lastProcessedNode = node;
                        
                    } else { // 1st-level folder.
                        
                        
                        bool isDuplicate = false;
                        for (std::list<GmicTreeNode*>::const_iterator it = _imp->firstLevelEntries.begin(); it != _imp->firstLevelEntries.end();++it)
                        {
                            if ((*it)->getName() == entryName) {
                                isDuplicate = true;
                                break;
                            }
                        }
                       
                        
                        // Detect if filter is in 'Testing/' (won't be count in number of filters).
                        //GtkWidget *const markup2ascii = gtk_label_new(0);
                        //gtk_label_set_markup(GTK_LABEL(markup2ascii),nentry);
                        //const char *_nentry = gtk_label_get_text(GTK_LABEL(markup2ascii));
                        //is_testing = !std::strcmp(_nentry,"Testing");
                        
                        if (!isDuplicate) {
                            
                            
                            GmicTreeNode* topLevelNode = new GmicTreeNode();
                            topLevelNode->setName(entryName);
                            _imp->firstLevelEntries.push_back(topLevelNode);
                            parent[level] = topLevelNode;
                            lastProcessedNode = topLevelNode;
                        }
                    }
                    ++level;
                }
            } else if (err >= 2) { // If entry defines a regular filter.
                
                cimg::strpare(entry,' ',false,true);
                
                char *nentry = entry;
                
                while (*nentry=='_') {
                    ++nentry; --level;
                }
                
                //Clamp level to [0,7]
                if (level < 0) {
                    level = 0;
                } else if (level > 7) {
                    level = 7;
                }
                
                cimg::strpare(nentry,' ',false,true); cimg::strpare(nentry,'\"',true);
                cimg::strpare(command,' ',false,true);
                cimg::strpare(arguments,' ',false,true);
                
                if (*nentry) {
                    
                    std::string entryName(nentry);

                    GmicTreeNode* node = new GmicTreeNode();
                    node->setName(entryName);
                    if (level) {
                        GmicTreeNode* p = parent[level - 1];
                        assert(p);
                        node->setParent(p);
                    } else {
                        _imp->firstLevelEntries.push_back(node);
                    }
                    node->setGmicCommand(command);
                    node->setGmicArguments(arguments);
                    lastProcessedNode = node;
                    
                    ++_imp->nPlugins;
                    
                    if (err >= 3) { // Filter has a specified preview command.
                        cimg::strpare(preview_command,' ',false,true);
                        char *const preview_mode = std::strchr(preview_command,'(');
                        double factor = 1;
                        char sep = 0;
                        if (preview_mode && std::sscanf(preview_mode+1,"%lf%c",&factor,&sep)==2 && factor>=0 && sep==')') {
                            *preview_mode = 0;
                        } else {
                            factor = -1;
                        }
                        node->setGmicPreviewCommand(preview_command);
                        node->setPreviewZoomFactor(factor);
                    } else {
                        node->setGmicPreviewCommand("_none_");
                        node->setPreviewZoomFactor(-1);
                    }
                
                    if (!is_testing) {
                        ++nb_available_filters;
                    } // Count only non-testing filters.
                }
            }
        } else { // Line is the continuation of an entry.

                cimg::strpare(++_line,' ',false,true);
                
                std::string toAppend(_line);
                assert(lastProcessedNode);
                
                lastProcessedNode->appendGmicArguments(toAppend);
        }
    }
    
    
    ///Build parameters recursively for all GmicTreeNode
    progressSetText("Updating parameters....");
    for (std::list<GmicTreeNode*>::iterator it = _imp->firstLevelEntries.begin(); it != _imp->firstLevelEntries.end(); ++it) {
        (*it)->parseParametersFromGmicArgs();
    }
    
    progressEnd();
}

int
GmicGimpParser::getNPlugins() const
{
    return _imp->nPlugins;
}

static void printRecursive(GmicTreeNode* node,int nTabs)
{
    std::string spaces;
    for (int i = 0; i < nTabs; ++i) {
        spaces.push_back(' ');
    }
    std::cout << spaces << node->getName() << std::endl;
    if (!node->getGmicCommand().empty()) {
        std::cout << spaces << "  COMMAND: " << node->getGmicCommand() << std::endl;
        std::cout << spaces << "  ARGS: " << node->getGmicArguments() << std::endl;
        if (!node->getGmicPreviewCommand().empty()) {
            std::cout << spaces << "  PREVIEW COMMAND: " << node->getGmicPreviewCommand() << std::endl;
            std::cout << spaces << "  PREVIEW FACTOR: " << node->getPreviewZoomFactor() << std::endl;
        }
    }
    const std::list<GmicTreeNode*>& children = node->getChildren();
    for (std::list<GmicTreeNode*>::const_iterator it = children.begin(); it != children.end(); ++it) {
        printRecursive(*it,nTabs + 4);
    }
}

void
GmicGimpParser::printTree()
{
    for (std::list<GmicTreeNode*>::iterator it = _imp->firstLevelEntries.begin(); it != _imp->firstLevelEntries.end(); ++it) {
        printRecursive(*it,4);
    }
}

void
GmicTreeNode::parseParametersFromGmicArgs()
{
    if (!_imp->arguments.empty()) {
        
        char argument_name[256] = { 0 }, _argument_type[32] = { 0 }, argument_arg[65536] = { 0 };
        
        const char* argument = _imp->arguments.c_str();
        for (; *argument; ) {
            
            int err = std::sscanf(argument,"%4095[^=]=%4095[ a-zA-Z_](%65535[^)]",
                                  argument_name,_argument_type,&(argument_arg[0]=0));
            if (err!=3) {
                err = std::sscanf(argument,"%4095[^=]=%4095[ a-zA-Z_][%65535[^]]",
                                           argument_name,_argument_type,argument_arg);
            }
            
            if (err!=3) {
                err = std::sscanf(argument,"%4095[^=]=%4095[ a-zA-Z_]{%65535[^}]",
                                           argument_name,_argument_type,argument_arg);
            }
            if (err>=2) {
                argument += std::strlen(argument_name) + std::strlen(_argument_type) + std::strlen(argument_arg) + 3;
                if (*argument) ++argument;
                cimg::strpare(argument_name,' ',false,true);
                cimg::strpare(argument_name,'\"',true);
                cimg::strunescape(argument_name);
                cimg::strpare(_argument_type,' ',false,true);
                cimg::strpare(argument_arg,' ',false,true);
                
                const bool is_silent_argument = (*_argument_type=='_');
                
                std::string argumentType(_argument_type + (is_silent_argument?1:0));
                std::string argumentName(argument_name);
                
#if defined(_WIN64)
                typedef unsigned long long pint;
#else
                typedef unsigned long pint;
#endif
  
                if (argumentType == "float") {
                    
                    float value = 0, min_value = 0, max_value = 100;
                    setlocale(LC_NUMERIC,"C");
                    std::sscanf(argument_arg,"%f%*c%f%*c%f",&value,&min_value,&max_value);
                    
                    FloatParam* param = new FloatParam(argumentName,1);
                    param->setRange(min_value, max_value);
                    param->setDefaultValue(0, value);
                    addParameterAndTakeOwnership(param);
                    
                } else if (argumentType == "int") {
                    
                    float value = 0, min_value = 0, max_value = 100;
                    setlocale(LC_NUMERIC,"C");
                    std::sscanf(argument_arg,"%f%*c%f%*c%f",&value,&min_value,&max_value);
                    
                    IntParam* param = new IntParam(argumentName,1);
                    param->setRange(min_value, max_value);
                    param->setDefaultValue(0, value);
                    addParameterAndTakeOwnership(param);
                    
                } else if (argumentType == "bool") {
                    
                    cimg::strpare(argument_arg,' ',false,true); cimg::strpare(argument_arg,'\"',true);
                    bool
                    value = !(!*argument_arg || !cimg::strcasecmp(argument_arg,"false") ||
                              (argument_arg[0]=='0' && argument_arg[1]==0));
                    
                    BooleanParam* param = new BooleanParam(argumentName);
                    param->setDefaultValue(0, value);
                    addParameterAndTakeOwnership(param);

                } else if (argumentType == "button") {
                    
                    //float alignment = 0;
                    //setlocale(LC_NUMERIC,"C");
                    //if (std::sscanf(argument_arg,"%f",&alignment)!=1) alignment = 0;
                    ButtonParam* param = new ButtonParam(argumentName);
                    addParameterAndTakeOwnership(param);

                    
                } else if (argumentType == "choice") {
                    
                    char s_entry[256] = { 0 }, end = 0; int err = 0;
                    unsigned int value = 0;
                    const char *entries = argument_arg;
                    if (std::sscanf(entries,"%u",&value)==1) {
                        entries+=cimg_snprintf(s_entry,sizeof(s_entry),"%u",value) + 1;
                    }
                    
                    ChoiceParam* param = new ChoiceParam(argumentName);
                    param->setDefaultValue(0, value);
                    
                    while (*entries) {
                        if ((err = std::sscanf(entries,"%4095[^,]%c",s_entry,&end))>0) {
                            entries += std::strlen(s_entry) + (err==2?1:0);
                            cimg::strpare(s_entry,' ',false,true); cimg::strpare(s_entry,'\"',true);
                            
                            std::string stdEntry(s_entry);
                            param->addOption(stdEntry);
                        } else {
                            break;
                        }
                    }
                    addParameterAndTakeOwnership(param);

      
                    
                } else if (argumentType == "text") {
                    
                    int line_number = 0;
                    char sep = 0;
                    
                    StringParam* param = new StringParam(argumentName);
                    if (std::sscanf(argument_arg,"%d%c",&line_number,&sep)==2 && sep==',' && line_number==1) {
                        // Multi-line entry
                        
                        param->setType(StringParam::eStringParamTypeMultiLineText);
        
                        
                        char s_label[256] = { 0 };
                        cimg_snprintf(s_label,sizeof(s_label),"  %s :     ",argument_name);
                        char *value = std::strchr(argument_arg,',') + 1;
                        cimg::strunescape(value);
                        cimg::strpare(value,' ',false,true);
                        cimg::strpare(value,'\"',true);
                        for (char *p = value; *p; ++p) {
                            if (*p == _dquote) {
                                *p='\"';
                            }
                        }
                        
                        param->setDefaultValue(0, std::string(value));
                        
                        
                    } else { // Single-line entry
                        param->setType(StringParam::eStringParamTypeText);

                        char *value = (line_number!=0 || sep!=',')?argument_arg:(std::strchr(argument_arg,',') + 1);
                        cimg::strpare(value,' ',false,true);
                        cimg::strpare(value,'\"',true);
                        for (char *p = value; *p; ++p) {
                            if (*p == _dquote) {
                                *p='\"';
                            }
                        }
                        
                        param->setDefaultValue(0, std::string(value));
                        
                    }
                    addParameterAndTakeOwnership(param);

                    
                } else if (argumentType == "file") {
                    
                    StringParam* param = new StringParam(argumentName);
                    param->setType(StringParam::eStringParamTypeFile);
                    
                    char *value = argument_arg;
                    cimg::strpare(value,' ',false,true);
                    cimg::strpare(value,'\"',true);
                    
                    param->setDefaultValue(0, std::string(value));
                    addParameterAndTakeOwnership(param);
                } else if (argumentType == "folder") {
                    
                    StringParam* param = new StringParam(argumentName);
                    param->setType(StringParam::eStringParamTypeFolder);
                    
                    char *value = argument_arg;
                    cimg::strpare(value,' ',false,true);
                    cimg::strpare(value,'\"',true);
                    
                    param->setDefaultValue(0, std::string(value));
                    addParameterAndTakeOwnership(param);
                } else if (argumentType == "color") {
                    
                    
                    float red = 0, green = 0, blue = 0, alpha = 255;
                    setlocale(LC_NUMERIC,"C");
                    
                    ///Gmic files contain [0-255] values!
                    const int err = std::sscanf(argument_arg,"%f%*c%f%*c%f%*c%f",&red,&green,&blue,&alpha);
                    
                    red = red < 0 ? 0 : red > 255 ? 255 : red;
                    green = green < 0 ? 0 : green > 255 ? 255 : green;
                    blue = blue < 0 ? 0 : blue > 255 ? 255 : blue;
 
                    int nDims;
                    if (err == 4) {
                        nDims = 4;
                    } else {
                        nDims = 3;
                    }
                    ColorParam* param = new ColorParam(argumentName,nDims);
                    addParameterAndTakeOwnership(param);
                    param->setDefaultValue(0, red / 255.f);
                    param->setDefaultValue(1, green / 255.f);
                    param->setDefaultValue(2, blue / 255.f);
                    if (nDims == 4) {
                        param->setDefaultValue(3, alpha / 255.f);
                    }
                    
                } else if (argumentType == "note" ||
                           argumentType == "const") { //< treat const as note, don't know what to do with them otherwise
                    
                    StringParam* param = new StringParam(argumentName);
                    param->setType(StringParam::eStringParamTypeLabel);
                    
                    char *value = argument_arg;
                    cimg::strpare(value,' ',false,true);
                    cimg::strpare(value,'\"',true);
                    
                    param->setDefaultValue(0, std::string(value));
                    addParameterAndTakeOwnership(param);
                    
                } else if (argumentType == "link") {
                    
                    char label[1024] = { 0 }, url[1024] = { 0 };
                    float alignment = 0.5f;
                    switch (std::sscanf(argument_arg,"%f,%1023[^,],%1023s",&alignment,label,url)) {
                        case 2 : std::strcpy(url,label); break;
                        case 1 : cimg_snprintf(url,sizeof(url),"%g",alignment); break;
                        case 0 : if (std::sscanf(argument_arg,"%1023[^,],%1023s",label,url)==1) std::strcpy(url,label); break;
                    }
                    cimg::strpare(label,' ',false,true);
                    cimg::strpare(label,'\"',true);
                    cimg::strunescape(label);
                    cimg::strpare(url,' ',false,true);
                    cimg::strpare(url,'\"',true);
                    
                    StringParam* param = new StringParam(argumentName);
                    param->setType(StringParam::eStringParamTypeLabel);
                    
                    
                    param->setDefaultValue(0, std::string(url));
                    addParameterAndTakeOwnership(param);
    
                    
                }
                //Ignore separators, they are not of any value for us
                else if (argumentType == "separator") {
                
                
                }
                else {
                    std::fprintf(cimg::output(),
                                    "\n[gmic_gimp]./error/ Found invalid parameter type '%s' for argument '%s'.\n",
                                    argumentType.c_str(),argument_name);
                    std::fflush(cimg::output());
                }
            } else { // if (err>=2) {
                break;
            }
        }

    }

    for (std::list<GmicTreeNode*>::iterator it = _imp->children.begin(); it != _imp->children.end(); ++it) {
        (*it)->parseParametersFromGmicArgs();
    }
}

