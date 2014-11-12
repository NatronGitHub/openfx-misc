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
#include <vector>
#include <list>
#include <cfloat>
#include <limits>

namespace Gmic {
    

class ParameterBase
{
public:
    
    ParameterBase(const std::string& label,int nDim);
    
    virtual ~ParameterBase();
    
    int getNDim() const;
    
    /**
     * @brief The label of the parameter as described by the .gmic file
     **/
    const std::string& getLabel() const;
    
    /**
     * @brief Same as the label but without spaces and starts with a lower-case letter.
     **/
    const std::string& getScriptName() const;
    
    /**
     * @brief A parameter is silent when it corresponds to the case exhibited in the documentation:
            You can also replace 'typedef' by '_typedef' to tell the plug-in not
            to update the image preview when the corresponding parameter is modified.
     **/
    bool isSilent() const;
    void setSilent(bool silent);
    
private:
    
    struct ParameterBasePrivate;
    ParameterBasePrivate* _imp;
};

template <typename T>
class Param : public ParameterBase
{
    
public:
    
    Param(const std::string& label,int nDim)
    : ParameterBase(label,nDim)
    , defaultValues(nDim)
    {
        for (int i = 0; i < nDim; ++i) {
            defaultValues[i] = T();
        }
    }
    
    virtual ~Param()
    {
        
    }
    
    void setDefaultValue(int dimIndex,const T& value) {
        defaultValues[dimIndex] = value;
    }
    
    const T& getDefaultValue(int dimIndex) const
    {
        return defaultValues[dimIndex];
    }
    
private:
    
    std::vector<T> defaultValues;
};

class IntParam : public Param<int>
{
public:
    
    IntParam(const std::string& label,int nDim)
    : Param<int>(label,nDim)
    , rangeMin(INT_MIN)
    , rangeMax(INT_MAX)
    {
        
    }
    
    void setRange(int min,int max)
    {
        rangeMin = min;
        rangeMax = max;
    }
    
    void getRange(int* min,int* max) const
    {
        *min = rangeMin;
        *max = rangeMax;
    }
    
private:
    
    int rangeMin,rangeMax;
};


class FloatParam : public Param<double>
{
public:
    
    FloatParam(const std::string& label,int nDim)
    : Param<double>(label,nDim)
    , rangeMin(-DBL_MAX)
    , rangeMax(DBL_MAX)
    {
        
    }
    
    void setRange(double min,double max)
    {
        rangeMin = min;
        rangeMax = max;
    }
    
    void getRange(double* min,double* max) const
    {
        *min = rangeMin;
        *max = rangeMax;
    }
    
private:
    
    double rangeMin,rangeMax;
};

class BooleanParam : public Param<bool>
{
public:
    
    BooleanParam(const std::string& label)
    : Param<bool>(label,1)
    {
        
    }
};

class ButtonParam : public Param<bool>
{
public:
    
    ButtonParam(const std::string& label)
    : Param<bool>(label,1)
    {
        
    }
};


class ChoiceParam : public Param<int>
{
public:
    
    ChoiceParam(const std::string& label)
    : Param<int>(label,1)
    , options()
    {
        
    }
    
    void addOption(const std::string& option)
    {
        options.push_back(option);
    }
    
    const std::list<std::string>& getOptions() const { return options; }
    
private:
    
    std::list<std::string> options;
};

class ColorParam : public Param<double>
{
public:
    
    ColorParam(const std::string& label,int nDim)
    : Param<double>(label,nDim)
    {
        
    }
};

class StringParam : public Param<std::string>
{
public:
    
    enum StringParamTypeEnum
    {
        eStringParamTypeLabel,
        eStringParamTypeUrl,
        eStringParamTypeText,
        eStringParamTypeMultiLineText,
        eStringParamTypeFile,
        eStringParamTypeFolder
    };
    
    StringParam(const std::string& label)
    : Param<std::string>(label,1)
    , type(eStringParamTypeLabel)
    {
        
    }
    
    void setType(StringParamTypeEnum type)
    {
        this->type = type;
    }
    
    StringParamTypeEnum getType() const {
        return type;
    }
    
private:
    
    StringParamTypeEnum type;
};

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
    
    /**
     * @brief Build the data structures representing each parameter of the filter (if it has any) from the raw
     * gmic arguments that were passed as a string beforehand via setGmicArguments().
     * This function is recursive and will create all parameters of the children nodes as needed.
     **/
    void parseParametersFromGmicArgs();
    
    void addParameterAndTakeOwnership(ParameterBase* param);
    
    const std::list<ParameterBase*>& getParameters() const;
    
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
     * @brief Resets the state of the parser as it is when the constructor only has been called.
     * Under the hood it deallocates all the node trees and parameters it has previously created.
     **/
    void reset();
    
    /**
     * @brief Extracts the "tree" structure from gmic defintion files where each node contains either a plug-in definition or a menu level.
     * The definition files are either to be found locally (via the GMIC_GIMP_PATH, HOME (or APPDATA) env. vars) or via online packages.
     * @param tryNetUpdateDownloads Tries to load gmic def file from the remote repositories indicated by the gimp_filter_sources command.
     * This requires a working internet connexion. Internally curl is used and if it doesn't work wget will be used.
     * @param errors[out] If not empty, this will be a buffer storing all errors that happened during the parsing.
     **/
    void parse(std::string* errors,bool tryNetUpdate,const char* locale = "en\0");
    
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
    
    virtual unsigned int get_verbosity_mode() {
#ifdef NDEBUG
        return 0;
#else
        return 2;
#endif
    }
    
    virtual void initProgress(const std::string& /*message*/) {}
    
    virtual void progressSetText(const std::string& /*message*/) {}
    
    virtual void progressEnd() {}
    
    struct GmicGimpParserPrivate;
    GmicGimpParserPrivate* _imp;
    
};

} //namespace Gmic

#endif /* defined(__Misc__GmicGimpParser__) */
