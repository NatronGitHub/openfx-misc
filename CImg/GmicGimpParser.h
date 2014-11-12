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
#include <list>

/**
 * @brief Gmic defines its plug-ins in a tree form with menus and submenus.
 * A GmicTreeNode is a menu entry, which can either point to a submenu or be a leaf.
 **/
class GmicTreeNode
{
    
public:
    
    GmicTreeNode();
    
    /**
     * @brief Deleting this node will also delete all its children.
     **/
    ~GmicTreeNode();
    
    GmicTreeNode* getParent() const;
    
    /**
     * @brief Set the given node to be the parent of this node. This will add this node as a child of the parent node
     * and remove it from the list of the children of the previous parent if it had any.
     **/
    void setParent(GmicTreeNode* parent);
    
    const std::list<GmicTreeNode*>& getChildren() const;
    
private:
    /**
     * @brief Tries to add child as a children of this node, if it doesn't already exists.
     **/
    bool tryAddChild(GmicTreeNode* child);
    
    /**
     * @brief Tries to remove the given child from the children list of this node it it exists.
     **/
    bool tryRemoveChild(GmicTreeNode* child);
    
public:
    
    
    bool isLeaf() const { return getChildren().empty(); }
    
    bool isTopLevelNode() const { return getParent() == 0; }
    
    const std::string& getName() const;
    void setName(const std::string& name);
    
    const std::string& getGmicCommand() const;
    void setGmicCommand(const std::string& command);
    
    const std::string& getGmicPreviewCommand() const;
    void setGmicPreviewCommand(const std::string& pCommand);
    
    const std::string& getGmicArguments() const;
    void setGmicArguments(const std::string& args);
    void appendGmicArguments(const std::string& args);
    
    double getPreviewZoomFactor() const;
    void setPreviewZoomFactor(double s);
private:
    
    struct GmicTreeNodePrivate;
    GmicTreeNodePrivate* _imp;
};

class GmicGimpParser
{
    
public:
    
    /**
     * @brief Ctor, doesn't do much more than initializing data structures
     **/
    GmicGimpParser();
    
    ~GmicGimpParser();
    
    
    /**
     * @brief Downloads gmic def file from the remote repositories indicated by the gimp_filter_sources command and parses them
     * to extract the "tree" structure where each node contains either a plug-in definition or a menu level.
     **/
    void parse(const char* locale = "en\0");
    
    /**
     * Gmic defines its plug-ins in a tree form with menus and submenus.
     * This returns the first level entries (which don't have parent menus).
     **/
    const std::list<GmicTreeNode*>& getFirstLevelEntries() const;

    /**
     * @brief For debug purposes.
     **/
    void printTree();
    
private:
    
    virtual unsigned int get_verbosity_mode(const bool normalized = true) { return 2; }
    
    virtual void initProgress(const std::string& /*message*/) {}
    
    virtual void progressSetText(const std::string& /*message*/) {}
    
    struct GmicGimpParserPrivate;
    GmicGimpParserPrivate* _imp;
    
};


#endif /* defined(__Misc__GmicGimpParser__) */
