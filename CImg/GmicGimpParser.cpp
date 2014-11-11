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

#include "GmicGimpParser.h"

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

struct GmicGimpParser::GmicGimpParserPrivate
{
    GmicGimpParserPrivate()
    {
        
    }
};

GmicGimpParser::GmicGimpParser()
: _imp(new GmicGimpParser::GmicGimpParserPrivate())
{
    
}

GmicGimpParser::~GmicGimpParser()
{
    delete _imp;
}

void
GmicGimpParser::downloadFilters(cimg_library::CImgList<char>& sources,cimg_library::CImgList<char>& invalid_servers)
{
    // Build list of filter sources.
    CImgList<float> _sources;
    CImgList<char> _names;
    char command[1024] = { 0 };
    CImg<char> gmic_additional_commands;

    
    cimg_snprintf(command,sizeof(command),"%s-gimp_filter_sources",
                  get_verbosity_mode()>4?"-debug ":get_verbosity_mode()>2?"":"-v -99 ");
    
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
    
    initProgress(" G'MIC : Update filters...");

    
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
                progressSetText(progressText);
            }
            
            cimg_snprintf(filename_tmp,sizeof(filename_tmp),"%s%c%s%s",
                          path_tmp,cimg_file_separator,_gmic_file_prefix,s_basename);
            cimg_snprintf(filename,sizeof(filename),"%s%c%s%s",
                          path_conf,cimg_file_separator,_gmic_file_prefix,s_basename);
            std::remove(filename_tmp);
            
            // Try curl first.
            if (get_verbosity_mode()) { // Verbose mode.
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
                if (get_verbosity_mode()) { // Verbose mode.
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
                    if (get_verbosity_mode()) { // Verbose mode.
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
                    if (get_verbosity_mode()) {
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

}

void
GmicGimpParser::parse(const bool tryNetUpdate,const char* locale)
{
   
    
    CImgList<char> gmic_entries;                   // The list of recognized G'MIC menu entries.
    CImgList<char> gmic_1stlevel_entries;          // The treepath positions of 1st-level G'MIC menu entries.
    CImgList<char> gmic_commands;                  // The list of corresponding G'MIC commands to process the image.
    CImgList<char> gmic_preview_commands;          // The list of corresponding G'MIC commands to preview the image.
    CImgList<char> gmic_arguments;                 // The list of corresponding needed filter arguments.
    CImgList<char> gmic_faves;                     // The list of favorites filters and their default parameters.
    CImgList<double> gmic_preview_factors;         // The list of default preview factors for each filter.
    CImgList<unsigned int> gmic_button_parameters; // The list of button parameters for the current filter.
    CImg<char> gmic_additional_commands;           // The buffer of additional G'MIC command implementations.
    CImg<float> computed_preview;                  // The last computed preview image.
    unsigned int nb_available_filters = 0;         // The number of available filters (non-testing).

    char command[1024] = { 0 };

    CImgList<char> sources;
    CImgList<char> invalid_servers;

    char filename[1024] = { 0 };
    const char *const path_conf = get_conf_path(), *const path_tmp = cimg::temporary_path();

    
    //Initialize resources
    gmic_additional_commands.assign();
    gmic_1stlevel_entries.assign();
    gmic_faves.assign();
    gmic_entries.assign(1);
    gmic_commands.assign(1);
    gmic_preview_commands.assign(1);
    gmic_preview_factors.assign(1);
    gmic_arguments.assign(1);
    
    if (tryNetUpdate) {
        downloadFilters(sources, invalid_servers);
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
        if (tryNetUpdate) {
 //           gimp_progress_pulse();
        }
    }
    
    if (!is_default_update) { // Add hardcoded default filters if no updates of the default commands.
        CImg<char>(data_gmic_def,1,size_data_gmic_def-1,1,1,true).move_to(_gmic_additional_commands);
        CImg<char>::string("\n#@gimp ________\n",false).unroll('y').move_to(_gmic_additional_commands);
    }
    CImg<char>::vector(0).move_to(_gmic_additional_commands);
    (_gmic_additional_commands>'y').move_to(gmic_additional_commands);
    
    // Add fave folder if necessary (make it before actually adding faves to make tree paths valids).
    CImgList<char> gmic_1stlevel_names;
    //GtkTreeIter iter, fave_iter, parent[8];
    char filename_gmic_faves[1024] = { 0 };
    //tree_view_store = gtk_tree_store_new(2,G_TYPE_UINT,G_TYPE_STRING);
    cimg_snprintf(filename_gmic_faves,sizeof(filename_gmic_faves),"%s%c%sgmic_faves",
                  path_conf,cimg_file_separator,_gmic_file_prefix);
    std::FILE *file_gmic_faves = std::fopen(filename_gmic_faves,"rb");
    //if (file_gmic_faves) {
        //gtk_tree_store_append(tree_view_store,&fave_iter,0);
        //gtk_tree_store_set(tree_view_store,&fave_iter,0,0,1,"<b>Faves</b>",-1);
        //const char *treepath = gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(tree_view_store),&fave_iter);
       // CImg<char>::vector(0).move_to(gmic_1stlevel_names);
      //  CImg<char>::string(treepath).move_to(gmic_1stlevel_entries);
    //}
    
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
                    ++nentry; --level;
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
                    if (level) {
                       // gtk_tree_store_append(tree_view_store,&parent[level],level?&parent[level-1]:0);
                       // gtk_tree_store_set(tree_view_store,&parent[level],0,0,1,nentry,-1);
                    } else { // 1st-level folder.
                        bool is_duplicate = false;
                        cimglist_for(gmic_1stlevel_names,l) {
                            if (!std::strcmp(nentry,gmic_1stlevel_names[l].data())) { // Folder name is a duplicate.
                                //if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(tree_view_store),&parent[level],
                                //                                       gmic_1stlevel_entries[l].data())) {
                                is_duplicate = true;
                                break;
                                //}
                            }
                        }
                        
                        // Detect if filter is in 'Testing/' (won't be count in number of filters).
                        //GtkWidget *const markup2ascii = gtk_label_new(0);
                        //gtk_label_set_markup(GTK_LABEL(markup2ascii),nentry);
                        //const char *_nentry = gtk_label_get_text(GTK_LABEL(markup2ascii));
                        //is_testing = !std::strcmp(_nentry,"Testing");
                        
                        if (!is_duplicate) {
                          //  gtk_tree_store_append(tree_view_store,&parent[level],level?&parent[level-1]:0);
                           // gtk_tree_store_set(tree_view_store,&parent[level],0,0,1,nentry,-1);
                            //const char *treepath = gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(tree_view_store),
                             //                                                          &parent[level]);
                            CImg<char>::string(nentry).move_to(gmic_1stlevel_names);
                            //CImg<char>::string(treepath).move_to(gmic_1stlevel_entries);
//                            unsigned int order = 0;
//                            for (unsigned int i = 0; i<4; ++i) {
//                                order<<=8;
//                                if (*_nentry) order|=(unsigned char)cimg::uncase(*(_nentry++));
//                            }
                        }
//                        gtk_widget_destroy(markup2ascii);
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
                    CImg<char>::string(nentry).move_to(gmic_entries);
                    CImg<char>::string(command).move_to(gmic_commands);
                    CImg<char>::string(arguments).move_to(gmic_arguments);
                    
                    if (err >= 3) { // Filter has a specified preview command.
                        cimg::strpare(preview_command,' ',false,true);
                        char *const preview_mode = std::strchr(preview_command,'(');
                        double factor = 1;
                        char sep = 0;
                        if (preview_mode && std::sscanf(preview_mode+1,"%lf%c",&factor,&sep)==2 && factor>=0 && sep==')')
                            *preview_mode = 0;
                        else factor = -1;
                        CImg<char>::string(preview_command).move_to(gmic_preview_commands);
                        CImg<double>::vector(factor).move_to(gmic_preview_factors);
                    } else {
                        CImg<char>::string("_none_").move_to(gmic_preview_commands);
                        CImg<double>::vector(-1).move_to(gmic_preview_factors);
                    }
               //     gtk_tree_store_append(tree_view_store,&iter,level?&parent[level-1]:0);
               //     gtk_tree_store_set(tree_view_store,&iter,0,gmic_entries.size()-1,1,nentry,-1);
                    if (!level) {
                 //       GtkWidget *const markup2ascii = gtk_label_new(0);
                  //      gtk_label_set_markup(GTK_LABEL(markup2ascii),nentry);
                    //    const char *_nentry = gtk_label_get_text(GTK_LABEL(markup2ascii));
                        unsigned int order = 0;
                      //  for (unsigned int i = 0; i<3; ++i) { order<<=8; if (*_nentry) order|=cimg::uncase(*(_nentry++)); }
                       // gtk_widget_destroy(markup2ascii);
                    }
                    if (!is_testing) {
                        ++nb_available_filters;
                    } // Count only non-testing filters.
                }
            }
        } else { // Line is the continuation of an entry.
            if (gmic_arguments) {
                
                if (gmic_arguments.back()) {
                    gmic_arguments.back().back() = ' ';
                }
                
                cimg::strpare(++_line,' ',false,true);
                
                gmic_arguments.back().append(CImg<char>(_line,std::strlen(_line)+1,1,1,1,true),'x');
            }
        }
    }
    
    
    // Load faves.
    char label[256] = { 0 };
   // indice_faves = gmic_entries.size();
    if (file_gmic_faves) {
        for (unsigned int line_nb = 1; std::fscanf(file_gmic_faves," %[^\n]",line)==1; ++line_nb) {
            char sep = 0;
            if (std::sscanf(line,"{%255[^}]}{%255[^}]}{%255[^}]}{%255[^}]%c",
                            label,entry,command,preview_command,&sep)==5 && sep=='}') {
                const char *_line = line + 8 + std::strlen(label) + std::strlen(entry) + std::strlen(command) +
                std::strlen(preview_command);
                int entry_found = -1, command_found = -1, preview_found = -1;
                unsigned int filter = 0;
//                for (filter = 1; filter<indice_faves; ++filter) {
//                    const bool
//                    is_entry_match = !std::strcmp(gmic_entries[filter].data(),entry),
//                    is_command_match = !std::strcmp(gmic_commands[filter].data(),command),
//                    is_preview_match = !std::strcmp(gmic_preview_commands[filter].data(),preview_command);
//                    if (is_entry_match) entry_found = filter;
//                    if (is_command_match) command_found = filter;
//                    if (is_preview_match) preview_found = filter;
//                    if (is_command_match && is_preview_match) break;
//                }
                
                CImg<char>::string(line).move_to(gmic_faves);
                // Get back '}' if necessary.
                for (char *p = std::strchr(label,_rbrace); p; p = std::strchr(p,_rbrace)) *p = '}';
                for (char *p = std::strchr(entry,_rbrace); p; p = std::strchr(p,_rbrace)) *p = '}';
                
//                if (filter>=indice_faves) { // Entry not found.
//                    CImg<char>::string(label).move_to(gmic_entries);
//                    CImg<char>::string("_none_").move_to(gmic_commands);
//                    CImg<char>::string("_none_").move_to(gmic_preview_commands);
//                    std::sprintf(line,"note = note{\"<span foreground=\"red\"><b>Warning : </b></span>This fave links to an "
//                                 "unreferenced entry/set of G'MIC commands :\n\n"
//                                 "   - '<span foreground=\"purple\">%s</span>' as the entry name (%s%s%s%s%s).\n\n"
//                                 "   - '<span foreground=\"purple\">%s</span>' as the command to compute the filter "
//                                 "(%s%s%s%s%s).\n\n"
//                                 "   - '<span foreground=\"purple\">%s</span>' as the command to preview the filter "
//                                 "(%s%s%s%s%s)."
//                                 "\"}",
//                                 entry,
//                                 entry_found>=0?"recognized, associated to <i>":"<b>not recognized</b>",
//                                 entry_found>=0?gmic_commands[entry_found].data():"",
//                                 entry_found>=0?", ":"",
//                                 entry_found>=0?gmic_preview_commands[entry_found].data():"",
//                                 entry_found>=0?"</i>":"",
//                                 command,
//                                 command_found>=0?"recognized, associated to <i>":"<b>not recognized</b>",
//                                 command_found>=0?gmic_entries[command_found].data():"",
//                                 command_found>=0?", ":"",
//                                 command_found>=0?gmic_preview_commands[command_found].data():"",
//                                 command_found>=0?"</i>":"",
//                                 preview_command,
//                                 preview_found>=0?"recognized, associated to <i>":"<b>not recognized</b>",
//                                 preview_found>=0?gmic_entries[preview_found].data():"",
//                                 preview_found>=0?", ":"",
//                                 preview_found>=0?gmic_commands[preview_found].data():"",
//                                 preview_found>=0?"</i>":"");
//                    
//                    CImg<char>::string(line).move_to(gmic_arguments);
//                    CImg<double>::vector(0).move_to(gmic_preview_factors);
//                    set_filter_nbparams(gmic_entries.size()-1,0);
//                } else { // Entry found.
//                    CImg<char>::string(label).move_to(gmic_entries);
//                    gmic_commands.insert(gmic_commands[filter]);
//                    gmic_preview_commands.insert(gmic_preview_commands[filter]);
//                    gmic_arguments.insert(gmic_arguments[filter]);
//                    gmic_preview_factors.insert(gmic_preview_factors[filter]);
//                    unsigned int nbp = 0;
//                    for (nbp = 0; std::sscanf(_line,"{%65533[^}]%c",arguments,&sep)==2 && sep=='}'; ++nbp) {
//                        // Get back '}' if necessary.
//                        for (char *p = std::strchr(arguments,_rbrace); p; p = std::strchr(p,_rbrace)) *p = '}';
//                        // Get back '\n' if necessary.
//                        for (char *p = std::strchr(arguments,_newline); p; p = std::strchr(p,_newline)) *p = '\n';
//                        set_fave_parameter(gmic_entries.size()-1,nbp,arguments);
//                        _line+=2 + std::strlen(arguments);
//                    }
//                    set_filter_nbparams(gmic_entries.size()-1,nbp);
//                }
//                gtk_tree_store_append(tree_view_store,&iter,&fave_iter);
//                gtk_tree_store_set(tree_view_store,&iter,0,gmic_entries.size()-1,1,label,-1);
            } else if (get_verbosity_mode())
                std::fprintf(cimg::output(),
                             "\n[gmic_gimp]./error/ Malformed line %u in fave file '%s' : '%s'.\n",
                             line_nb,filename_gmic_faves,line);
        }
        std::fclose(file_gmic_faves);
    }
    
    if (tryNetUpdate) {
        //gimp_progress_update(1);
        //gimp_progress_end();
    }
//    return invalid_servers;
}




