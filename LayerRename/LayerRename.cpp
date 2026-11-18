/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
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
 * OFX LayerRename plugin.
 * Rename all the layers (image planes) of the input with find/replace.
 */

#include <list>
#include <string>
#include <vector>
#include <set>
#include <regex>

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsMacros.h"
#include "ofxsCopier.h"
#include "ofxsMultiPlane.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

// Names of the planes that have a special meaning for hosts (motion vectors, disparity, color):
// a renamed layer must not take one of them, or it would be confused with that plane.
static bool
isReservedPlaneName(const std::string& name)
{
    static const char* const reserved[] = {
        "Color", "RGBA", "RGB", "Alpha",
        "Forward", "Backward", "DisparityLeft", "DisparityRight",
        NULL
    };
    for (const char* const* r = reserved; *r != NULL; ++r) {
        if (name == *r) {
            return true;
        }
    }
    // the OFX plane identifiers (kFnOfxImagePlane*), e.g. "uk.co.thefoundry.OfxImagePlaneColour"
    return name.compare(0, 30, "uk.co.thefoundry.OfxImagePlane") == 0;
}

#define kPluginName "LayerRenameOFX"
#define kPluginGrouping "Channel"
#define kPluginDescription \
    "Rename the layers (image planes) of the input using find and replace.\n" \
    "\n" \
    "For example, if the input has the layers scene.001.DiffCol and scene.001.IndexMA, " \
    "Find \"scene.001.\" and Replace with \"\" (Match: Prefix) outputs the layers DiffCol and IndexMA, " \
    "so that nodes downstream keep working when the render is replaced by scene.002.\n" \
    "\n" \
    "Only the layer name is changed: the channels (e.g. R, G, B or X, Y, Z) and the pixels are kept. " \
    "A layer named scene.001.DiffCol.RGB in a layer menu is the layer scene.001.DiffCol with the channels R, G and B.\n" \
    "\n" \
    "The Color layer (the main RGBA image) keeps its name, unless \"Rename Color Layer\" is checked: " \
    "the name \"Color\" is then renamed too, and the result is an extra layer with a copy of the Color layer. " \
    "The Color layer itself is always kept, since every image has one. " \
    "The motion vector (Forward, Backward) and disparity (DisparityLeft, DisparityRight) layers are never renamed.\n" \
    "\n" \
    "If a new name is empty, is the name of a special layer (Color, RGBA, RGB, Alpha, Forward, Backward, DisparityLeft, DisparityRight), or is already used by another layer of the output (a layer that is not renamed, " \
    "or a layer renamed before it, in the input order), the layer keeps its original name, and a warning is displayed. " \
    "If that name was also taken by a renamed layer, the layer is removed from the output.\n" \
    "\n" \
    "This plugin requires a host that supports multiple image planes with arbitrary names " \
    "(such as Natron). On other hosts, it does nothing (it is an identity)."

#define kPluginIdentifier "net.sf.openfx.LayerRename"
// History:
// version 1.0: initial version (issue #141)
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamFind "find"
#define kParamFindLabel "Find"
#define kParamFindHint \
    "Text to find in each layer name. If empty, no layer is renamed."

#define kParamReplace "replace"
#define kParamReplaceLabel "Replace with"
#define kParamReplaceHint \
    "Replacement text. With the \"Regular expression\" match, $1 to $9 are replaced by the corresponding " \
    "parenthesized sub-expressions, $& by the whole match, and $$ by $."

#define kParamMatch "match"
#define kParamMatchLabel "Match"
#define kParamMatchHint \
    "How the Find text is matched in each layer name."
#define kParamMatchOptionPrefix "Prefix", "Replace Find with Replace only if the layer name starts with Find.", "prefix"
#define kParamMatchOptionAnywhere "Anywhere", "Replace every occurrence of Find in the layer name.", "anywhere"
#define kParamMatchOptionRegex "Regular expression", "Find is an ECMAScript regular expression (as in JavaScript), and every match is replaced by Replace, which may contain back-references ($1...). Use ^ and $ to anchor the expression at the start or end of the name.", "regex"
enum MatchEnum
{
    eMatchPrefix = 0,
    eMatchAnywhere,
    eMatchRegex,
};

#define kParamRenameColor "renameColor"
#define kParamRenameColorLabel "Rename Color Layer"
#define kParamRenameColorHint \
    "Also rename the Color layer, using \"Color\" as its name. " \
    "The Color layer is always kept, so this adds a layer with the new name that is a copy of the Color layer."

#define kParamRenames "renames"
#define kParamRenamesLabel "Renamed Layers"
#define kParamRenamesHint \
    "The layers that are renamed, as \"old name -> new name\", and the conflicts. " \
    "Read-only, and only refreshed when a parameter or the input changes."

#define kColorPlaneName "Color" // the name of the color plane for the renaming (kOfxMultiplaneColorPlaneLabel)

static bool gIsMultiPlanar = false;

// A plane of the output, and the input plane it is copied from.
struct PlaneRename
{
    std::string srcPlane; // OFX plane string of the input plane
    std::string dstPlane; // OFX plane string of the output plane
    std::string srcName;  // name shown to the user
    std::string dstName;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class LayerRenamePlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    LayerRenamePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _find(NULL)
        , _replace(NULL)
        , _match(NULL)
        , _renameColor(NULL)
        , _renames(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip);
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip);
        _find = fetchStringParam(kParamFind);
        _replace = fetchStringParam(kParamReplace);
        _match = fetchChoiceParam(kParamMatch);
        _renameColor = fetchBooleanParam(kParamRenameColor);
        _renames = fetchStringParam(kParamRenames);
        assert(_find && _replace && _match && _renameColor && _renames);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime
#ifdef OFX_EXTENSIONS_NUKE
                            , int& view
                            , std::string& plane
#endif
                            ) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
#ifdef OFX_EXTENSIONS_NUKE
    virtual OfxStatus getClipComponents(const ClipComponentsArguments& args, ClipComponentsSetter& clipComponents) OVERRIDE FINAL;
#endif
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    /**
     * @brief Compute the planes of the output and the input plane each one is copied from.
     * The color plane is always first. Returns true if any plane is renamed.
     * If messages is not NULL, it receives the renames and conflicts, one per line,
     * and error receives an error (e.g. an invalid regular expression).
     */
    bool getPlaneRenames(double time,
                         std::vector<PlaneRename>* renames,
                         std::string* messages,
                         std::string* warning,
                         std::string* error) const;

    /** @brief rename a plane name with the current parameters. Returns false if the name is unchanged. */
    static bool renamePlane(const std::string& name,
                            MatchEnum match,
                            const std::string& find,
                            const std::string& replace,
                            const std::regex* re,
                            std::string* newName);

    /** @brief refresh the Renamed Layers param and the persistent message */
    void updateRenames();

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip* _dstClip;
    Clip* _srcClip;
    StringParam* _find;
    StringParam* _replace;
    ChoiceParam* _match;
    BooleanParam* _renameColor;
    StringParam* _renames;
};

bool
LayerRenamePlugin::renamePlane(const std::string& name,
                               MatchEnum match,
                               const std::string& find,
                               const std::string& replace,
                               const std::regex* re,
                               std::string* newName)
{
    if ( find.empty() ) {
        return false;
    }
    switch (match) {
    case eMatchPrefix:
        if (name.compare(0, find.size(), find) != 0) {
            return false;
        }
        *newName = replace + name.substr( find.size() );
        break;
    case eMatchAnywhere: {
        std::string result;
        std::size_t pos = 0;
        std::size_t found;
        while ( ( found = name.find(find, pos) ) != std::string::npos ) {
            result.append(name, pos, found - pos);
            result.append(replace);
            pos = found + find.size();
        }
        result.append(name, pos, std::string::npos);
        *newName = result;
        break;
    }
    case eMatchRegex:
        if (!re) {
            return false;
        }
        try {
            *newName = std::regex_replace(name, *re, replace);
        } catch (const std::regex_error&) {
            // e.g. error_complexity or error_stack while matching: the plane is not renamed
            return false;
        }
        break;
    }

    return *newName != name;
}

bool
LayerRenamePlugin::getPlaneRenames(double time,
                                   std::vector<PlaneRename>* renames,
                                   std::string* messages,
                                   std::string* warning,
                                   std::string* error) const
{
    renames->clear();
    bool renamed = false;
    const int srcNComps = (_srcClip && _srcClip->isConnected()) ? _srcClip->getPixelComponentCount() : 0;
    // the color plane of the output is always the color plane of the input
    {
        PlaneRename color;
        color.srcPlane = color.dstPlane = kFnOfxImagePlaneColour;
        color.srcName = color.dstName = kColorPlaneName;
        renames->push_back(color);
    }
    if ( !gIsMultiPlanar || !_srcClip || !_srcClip->isConnected() ) {
        return false;
    }

    std::string find, replace;
    _find->getValueAtTime(time, find);
    _replace->getValueAtTime(time, replace);
    MatchEnum match = (MatchEnum)_match->getValueAtTime(time);
    bool renameColor = _renameColor->getValueAtTime(time);

    std::regex re;
    bool reValid = false;
    if ( (match == eMatchRegex) && !find.empty() ) {
        try {
            re.assign(find, std::regex::ECMAScript);
            reValid = true;
        } catch (const std::regex_error& e) {
            if (error) {
                *error = std::string("Invalid regular expression \"") + find + "\": " + e.what();
            }
        }
    }

    std::vector<std::string> planes;
    _srcClip->getPlanesPresent(&planes);

    // First pass: compute the new names. The names of the planes that are not renamed are reserved.
    struct Candidate
    {
        std::string srcPlane;
        MultiPlane::ImagePlaneDesc desc;
        std::string name;
        std::string newName;
        bool rename;
    };
    std::vector<Candidate> candidates;
    std::set<std::string> taken; // plane IDs and labels used in the output
    taken.insert(kColorPlaneName);
    taken.insert(kFnOfxImagePlaneColour);
    for (std::size_t i = 0; i < planes.size(); ++i) {
        Candidate c;
        c.srcPlane = planes[i];
        c.rename = false;
        if (planes[i] == kFnOfxImagePlaneColour) {
            // the color plane is always in the output: only a copy may be added
            if (!renameColor || srcNComps == 0) {
                continue;
            }
            c.desc = MultiPlane::ImagePlaneDesc::mapNCompsToColorPlane(srcNComps);
            c.name = kColorPlaneName;
        } else {
            c.desc = MultiPlane::ImagePlaneDesc::mapOFXPlaneStringToPlane(planes[i]);
            c.name = c.desc.getPlaneID();
            if ( (planes[i].compare(0, sizeof(kNatronOfxImageComponentsPlaneName) - 1, kNatronOfxImageComponentsPlaneName) != 0) ||
                 !c.desc ||
                 isReservedPlaneName(c.name) ) {
                // not a Natron custom plane, or a plane with a special name (e.g. motion vectors or
                // disparity, or a custom plane literally named Forward or DisparityLeft): never renamed
                taken.insert(planes[i]);
                taken.insert( c.desc.getPlaneLabel() );
                candidates.push_back(c);
                continue;
            }
        }
        c.rename = renamePlane(c.name, match, find, replace, reValid ? &re : NULL, &c.newName);
        if (!c.rename) {
            if (planes[i] == kFnOfxImagePlaneColour) {
                continue; // already in the output
            }
            taken.insert(c.name);
            taken.insert( c.desc.getPlaneLabel() );
        }
        candidates.push_back(c);
    }

    // Second pass: give the new names, in the input order
    std::string msgs, warn;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const Candidate& c = candidates[i];
        PlaneRename r;
        r.srcPlane = c.srcPlane;
        r.srcName = c.name;
        if (!c.rename) {
            r.dstPlane = c.srcPlane;
            r.dstName = c.name;
            renames->push_back(r);
            continue;
        }
        const bool isColor = (c.srcPlane == kFnOfxImagePlaneColour);
        std::string problem;
        if ( c.newName.empty() ) {
            problem = "empty name";
        } else if ( (c.newName.find(kNatronOfxImageComponentsPlaneLabel) != std::string::npos) ||
                    (c.newName.find(kNatronOfxImageComponentsPlaneChannelsLabel) != std::string::npos) ||
                    (c.newName.find(kNatronOfxImageComponentsPlaneChannel) != std::string::npos) ) {
            // separators of the Natron custom plane encoding (ofxNatron.h): a plane name must not contain them
            problem = "invalid name";
        } else if ( isReservedPlaneName(c.newName) ) {
            problem = "reserved name";
        } else if ( taken.count(c.newName) ) {
            problem = "name already used";
        }
        if ( problem.empty() ) {
            MultiPlane::ImagePlaneDesc newDesc(c.newName, "", c.desc.getChannelsLabel(), c.desc.getChannels());
            r.dstPlane = MultiPlane::ImagePlaneDesc::mapPlaneToOFXPlaneString(newDesc);
            r.dstName = c.newName;
            taken.insert(c.newName);
            renames->push_back(r);
            renamed = true;
            msgs += c.name + " -> " + c.newName + "\n";
            continue;
        }
        // conflict
        if (isColor) {
            // the color plane is in the output anyway, the copy is just not added
            warn += c.name + " -> " + c.newName + ": " + problem + ", not renamed\n";
        } else if ( taken.count(c.name) ) {
            warn += c.name + " -> " + c.newName + ": " + problem + ", and " + c.name + " is used by another renamed layer: removed\n";
            renamed = true; // the output differs from the input: a plane is removed
        } else {
            r.dstPlane = c.srcPlane;
            r.dstName = c.name;
            taken.insert(c.name);
            taken.insert( c.desc.getPlaneLabel() );
            renames->push_back(r);
            warn += c.name + " -> " + c.newName + ": " + problem + ", kept as " + c.name + "\n";
        }
        // the color copy that is not added, or a plane kept with its name: the output is unchanged
    }
    if (messages) {
        *messages = msgs + warn;
        if ( !messages->empty() ) {
            messages->erase(messages->size() - 1); // trailing newline
        }
    }
    if (warning) {
        *warning = warn;
    }

    return renamed;
} // LayerRenamePlugin::getPlaneRenames

// Sets the Renamed Layers param: called from changedParam and changedClip (kOfxActionInstanceChanged,
// as the OFX spec requires), and from getClipPreferences, which is a Natron-only exception to the spec
// (see getClipPreferences). Does nothing unless gIsMultiPlanar.
void
LayerRenamePlugin::updateRenames()
{
    if (!gIsMultiPlanar) {
        return;
    }
    std::vector<PlaneRename> renames;
    std::string messages, warning, error;
    getPlaneRenames(timeLineGetTime(), &renames, &messages, &warning, &error);
    std::string current;
    _renames->getValue(current);
    if (current != messages) {
        _renames->setValue(messages);
    }
    if ( !error.empty() ) {
        setPersistentMessage(Message::eMessageError, "", error);
    } else if ( !warning.empty() ) {
        setPersistentMessage(Message::eMessageWarning, "", "Some layers could not be renamed:\n" + warning);
    } else {
        clearPersistentMessage();
    }
}

void
LayerRenamePlugin::render(const RenderArguments &args)
{
    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );

    std::list<std::string> planes;
#ifdef OFX_EXTENSIONS_NUKE
    if (gIsMultiPlanar) {
        planes = args.planes;
    }
#endif
    if ( planes.empty() ) {
        planes.push_back(kFnOfxImagePlaneColour);
    }

    std::vector<PlaneRename> renames;
    getPlaneRenames(args.time, &renames, NULL, NULL, NULL);

    for (std::list<std::string>::const_iterator it = planes.begin(); it != planes.end(); ++it) {
        auto_ptr<Image> dst;
        auto_ptr<const Image> src;
        const PlaneRename* rename = NULL;
        for (std::size_t i = 0; i < renames.size(); ++i) {
            if (renames[i].dstPlane == *it) {
                rename = &renames[i];
                break;
            }
        }
#ifdef OFX_EXTENSIONS_NUKE
        if (gIsMultiPlanar) {
            dst.reset( _dstClip->fetchImagePlane( args.time, args.renderView, it->c_str() ) );
            if ( rename && _srcClip && _srcClip->isConnected() ) {
                src.reset( _srcClip->fetchImagePlane( args.time, args.renderView, rename->srcPlane.c_str() ) );
            }
        } else
#endif
        {
            dst.reset( _dstClip->fetchImage(args.time) );
            if ( _srcClip && _srcClip->isConnected() ) {
                src.reset( _srcClip->fetchImage(args.time) );
            }
        }
        if ( !dst.get() ) {
            throwSuiteStatusException(kOfxStatFailed);
        }
        checkBadRenderScaleOrField(dst, args);
        if ( !src.get() ) {
            // no input, or a plane we do not produce (should not happen): black and transparent
            fillBlack( *this, args.renderWindow, args.renderScale, dst.get() );
            continue;
        }
        checkBadRenderScaleOrField(src, args);
        if ( ( src->getPixelDepth() != dst->getPixelDepth() ) ||
             ( src->getPixelComponentCount() != dst->getPixelComponentCount() ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        copyPixels( *this, args.renderWindow, args.renderScale, src.get(), dst.get() );
    }
} // LayerRenamePlugin::render

bool
LayerRenamePlugin::isIdentity(const IsIdentityArguments &args,
                              Clip * &identityClip,
                              double & /*identityTime*/
#ifdef OFX_EXTENSIONS_NUKE
                              , int& /*view*/
                              , std::string& /*plane*/
#endif
                              )
{
    // Without multiplane support, there is nothing to rename.
    // With multiplane support, identity if no plane is renamed (all planes are then copied unchanged).
    std::vector<PlaneRename> renames;
    if ( !gIsMultiPlanar || !getPlaneRenames(args.time, &renames, NULL, NULL, NULL) ) {
        identityClip = _srcClip;

        return true;
    }

    return false;
}

void
LayerRenamePlugin::getClipPreferences(ClipPreferencesSetter & /*clipPreferences*/)
{
    // The planes of the input may have changed upstream: refresh the list of renames.
    // This is a Natron-only exception to the OFX spec, which says (ofxParam.h, paramSetValue):
    // "paramSetValue should only be called from within a ::kOfxActionInstanceChanged or interact action."
    // Natron allows setting param values in getClipPreferences. updateRenames() does nothing unless
    // gIsMultiPlanar (a multiplane host with arbitrary plane names: in practice, Natron).
    updateRenames();
}

#ifdef OFX_EXTENSIONS_NUKE
OfxStatus
LayerRenamePlugin::getClipComponents(const ClipComponentsArguments& args,
                                     ClipComponentsSetter& clipComponents)
{
    assert(gIsMultiPlanar);
    // No pass-through: the input planes must not appear with their old names in the output.
    // Every output plane, renamed or not, is listed here and copied in render().
    clipComponents.setPassThroughClip(NULL, args.time, args.view);

    std::vector<PlaneRename> renames;
    getPlaneRenames(args.time, &renames, NULL, NULL, NULL);

    std::set<std::string> srcPlanes;
    for (std::size_t i = 0; i < renames.size(); ++i) {
        clipComponents.addClipPlane(*_dstClip, renames[i].dstPlane);
        if ( _srcClip && srcPlanes.insert(renames[i].srcPlane).second ) {
            clipComponents.addClipPlane(*_srcClip, renames[i].srcPlane);
        }
    }

    return kOfxStatOK;
}
#endif

void
LayerRenamePlugin::changedParam(const InstanceChangedArgs & /*args*/,
                                const std::string &paramName)
{
    if ( (paramName == kParamFind) || (paramName == kParamReplace) || (paramName == kParamMatch) || (paramName == kParamRenameColor) ) {
        updateRenames();
    }
}

void
LayerRenamePlugin::changedClip(const InstanceChangedArgs & /*args*/,
                               const std::string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) || (clipName == kOfxImageEffectOutputClipName) ) {
        updateRenames();
    }
}

mDeclarePluginFactory(LayerRenamePluginFactory, {ofxsThreadSuiteCheck();}, {});

void
LayerRenamePluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthHalf);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif

#ifdef OFX_EXTENSIONS_NUKE
    // Arbitrary planes need the plane suite V2 (clipGetImagePlane with a view) and the
    // getClipComponents action, i.e. the Natron multiplane extension (ofxNatron.h).
    // Nuke only has the plane suite V1, with hard-coded planes.
    // optional=true: fetchSuite() returns NULL instead of throwing when the host lacks the suite
    gIsMultiPlanar = getImageEffectHostDescription()->isMultiPlanar && fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true);
    if (gIsMultiPlanar) {
        desc.setIsMultiPlanar(true);
        // Block the planes not listed in getClipComponents: the old names must disappear.
        desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelBlockAllNonRenderedPlanes);
    }
#endif
} // LayerRenamePluginFactory::describe

void
LayerRenamePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                            ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        StringParamDescriptor* param = desc.defineStringParam(kParamFind);
        param->setLabel(kParamFindLabel);
        param->setHint(kParamFindHint);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param); // the output planes depend on it
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamReplace);
        param->setLabel(kParamReplaceLabel);
        param->setHint(kParamReplaceHint);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param); // the output planes depend on it
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamMatch);
        param->setLabel(kParamMatchLabel);
        param->setHint(kParamMatchHint);
        assert(param->getNOptions() == eMatchPrefix);
        param->appendOption(kParamMatchOptionPrefix);
        assert(param->getNOptions() == eMatchAnywhere);
        param->appendOption(kParamMatchOptionAnywhere);
        assert(param->getNOptions() == eMatchRegex);
        param->appendOption(kParamMatchOptionRegex);
        param->setDefault( (int)eMatchPrefix );
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param); // the output planes depend on it
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamRenameColor);
        param->setLabel(kParamRenameColorLabel);
        param->setHint(kParamRenameColorHint);
        param->setDefault(false);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param); // the output planes depend on it
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamRenames);
        param->setLabel(kParamRenamesLabel);
        param->setHint(kParamRenamesHint);
        param->setStringType(eStringTypeMultiLine);
        param->setEnabled(false);
        param->setIsPersistent(false);
        param->setEvaluateOnChange(false);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // LayerRenamePluginFactory::describeInContext

ImageEffect*
LayerRenamePluginFactory::createInstance(OfxImageEffectHandle handle,
                                         ContextEnum /*context*/)
{
    return new LayerRenamePlugin(handle);
}

static LayerRenamePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
