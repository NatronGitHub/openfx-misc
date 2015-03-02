/*
 OFX Roto plugin.
 
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
 
 
 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.
 
 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England
 
 */

/*
   Although the indications from nuke/fnOfxExtensions.h were followed, and the
   kFnOfxImageEffectActionGetTransform action was implemented in the Support
   library, that action is never called by the Nuke host, so it cannot be tested.
   The code is left here for reference or for further extension.

   There is also an open question about how the last plugin in a transform chain
   may get the concatenated transform from upstream, the untransformed source image,
   concatenate its own transform and apply the resulting transform in its render
   action. Should the host be doing this instead?
*/

#include "CornerPin.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ofxsOGLTextRenderer.h"
#include "ofxsTransform3x3.h"

#define kPluginName "CornerPinOFX"
#define kPluginMaskedName "CornerPinMaskedOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription \
"Allows an image to fit another in translation, rotation and scale.\n" \
"The resulting transform is a translation if 1 point is enabled, a " \
"similarity if 2 are enabled, an affine transform if 3 are enabled, " \
"and a homography if they are all enabled."
#define kPluginIdentifier "net.sf.openfx.CornerPinPlugin"
#define kPluginMaskedIdentifier "net.sf.openfx.CornerPinMaskedPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define POINT_SIZE 5
#define POINT_TOLERANCE 6

#define kGroupTo "to"
#define kGroupToLabel "To"
static const char* const kParamTo[4] = {
    "to1",
    "to2",
    "to3",
    "to4"
};

static const char* const kParamEnable[4] = {
    "enable1",
    "enable2",
    "enable3",
    "enable4"
};
#define kParamEnableHint "Enables the point on the left."

#define kGroupFrom "from"
#define kGroupFromLabel "From"
static const char* const kParamFrom[4] = {
    "from1",
    "from2",
    "from3",
    "from4"
};


#define kParamCopyFrom "copyFrom"
#define kParamCopyFromLabel "Copy \"From\" points"
#define kParamCopyFromHint "Copy the content from the \"to\" points to the \"from\" points."

#define kParamCopyTo "copyTo"
#define kParamCopyToLabel "Copy \"To\" points"
#define kParamCopyToHint "Copy the content from the \"from\" points to the \"to\" points."

#define kParamCopyInputRoD "setToInputRod"
#define kParamCopyInputRoDLabel "Set to input rod"
#define kParamCopyInputRoDHint "Copy the values from the source region of definition into the \"to\" points."

#define kParamOverlayPoints "overlayPoints"
#define kParamOverlayPointsLabel "Overlay points"
#define kParamOverlayPointsHint "Whether to display the \"from\" or the \"to\" points in the overlay"

#define kGroupExtraMatrix "transformMatrix"
#define kGroupExtraMatrixLabel "Extra matrix"
#define kGroupExtraMatrixHint "This matrix gets concatenated to the transform defined by the other parameters."
#define kParamExtraMatrixRow1 "row1"
#define kParamExtraMatrixRow2 "row2"
#define kParamExtraMatrixRow3 "row3"

#define kParamTransformInteractive "interactive"
#define kParamTransformInteractiveLabel "Interactive Update"
#define kParamTransformInteractiveHint "If checked, update the parameter values during interaction with the image viewer, else update the values when pen is released."

#define POINT_INTERACT_LINE_SIZE_PIXELS 20

using namespace OFX;


/**
 * \brief Compute the cross-product of two vectors
 *
 */
inline OFX::Point3D
crossprod(const OFX::Point3D& a, const OFX::Point3D& b)
{
    OFX::Point3D c;
    c.x = a.y * b.z - a.z * b.y;
    c.y = a.z * b.x - a.x * b.z;
    c.z = a.x * b.y - a.y * b.x;
    return c;
}

/**
 * brief Constructs a 3x3  matrix
 **/
inline OFX::Matrix3x3
matrix33_from_columns(const OFX::Point3D &m0, const OFX::Point3D &m1, const OFX::Point3D &m2) {
    OFX::Matrix3x3 M;
    M.a = m0.x; M.b = m1.x; M.c = m2.x;
    M.d = m0.y; M.e = m1.y; M.f = m2.y;
    M.g = m0.z; M.h = m1.z; M.i = m2.z;
    return M;
}

/**
 * \brief Compute a homography from 4 points correspondences
 * \param p1 source point
 * \param p2 source point
 * \param p3 source point
 * \param p4 source point
 * \param q1 target point
 * \param q2 target point
 * \param q3 target point
 * \param q4 target point
 * \return the homography matrix that maps pi's to qi's
 *
 Using four point-correspondences pi ↔ pi^, we can set up an equation system to solve for the homography matrix H.
 An algorithm to obtain these parameters requiring only the inversion of a 3 × 3 equation system is as follows.
 From the four point-correspondences pi ↔ pi^ with (i ∈ {1, 2, 3, 4}),
 compute h1 = (p1 × p2 ) × (p3 × p4 ), h2 = (p1 × p3 ) × (p2 × p4 ), h3 = (p1 × p4 ) × (p2 × p3 ).
 Also compute h1^ , h2^ , h3^ using the same principle from the points pi^.
 Now, the homography matrix H can be obtained easily from
 H · [h1 h2 h3] = [h1^ h2^ h3^],
 which only requires the inversion of the matrix [h1 h2 h3].
 
 Algo from:
 http://www.dirk-farin.net/publications/phd/text/AB_EfficientComputationOfHomographiesFromFourCorrespondences.pdf
 */
inline bool
homography_from_four_points(const OFX::Point3D &p1, const OFX::Point3D &p2, const OFX::Point3D &p3, const OFX::Point3D &p4,
                            const OFX::Point3D &q1, const OFX::Point3D &q2, const OFX::Point3D &q3, const OFX::Point3D &q4,
                            OFX::Matrix3x3 *H)
{
    OFX::Matrix3x3 invHp;
    OFX::Matrix3x3 Hp = matrix33_from_columns(crossprod(crossprod(p1,p2),crossprod(p3,p4)),
                                        crossprod(crossprod(p1,p3),crossprod(p2,p4)),
                                        crossprod(crossprod(p1,p4),crossprod(p2,p3)));
    double detHp = ofxsMatDeterminant(Hp);
    if (detHp == 0.) {
        return false;
    }
    OFX::Matrix3x3  Hq = matrix33_from_columns(crossprod(crossprod(q1,q2),crossprod(q3,q4)),
                                        crossprod(crossprod(q1,q3),crossprod(q2,q4)),
                                        crossprod(crossprod(q1,q4),crossprod(q2,q3)));
    double detHq = ofxsMatDeterminant(Hq);
    if (detHq == 0.) {
        return false;
    }
    invHp = ofxsMatInverse(Hp,detHp);
    *H = Hq * invHp;
    return true;
}

inline bool
affine_from_three_points(const OFX::Point3D &p1, const OFX::Point3D &p2, const OFX::Point3D &p3,
                            const OFX::Point3D &q1, const OFX::Point3D &q2, const OFX::Point3D &q3,
                            OFX::Matrix3x3 *H)
{
    OFX::Matrix3x3 invHp;
    OFX::Matrix3x3 Hp = matrix33_from_columns(p1,p2,p3);
    double detHp = ofxsMatDeterminant(Hp);
    if (detHp == 0.) {
        return false;
    }
    OFX::Matrix3x3  Hq = matrix33_from_columns(q1,q2,q3);
    double detHq = ofxsMatDeterminant(Hq);
    if (detHq == 0.) {
        return false;
    }
    invHp = ofxsMatInverse(Hp,detHp);
    *H = Hq * invHp;
    return true;
}

inline bool
similarity_from_two_points(const OFX::Point3D &p1, const OFX::Point3D &p2,
                           const OFX::Point3D &q1, const OFX::Point3D &q2,
                           OFX::Matrix3x3 *H)
{
    // Generate a third point so that p1p3 is orthogonal to p1p2, and compute the affine transform
    OFX::Point3D p3, q3;
    p3.x = p1.x - (p2.y - p1.y);
    p3.y = p1.y + (p2.x - p1.x);
    p3.z = 1.;
    q3.x = q1.x - (q2.y - q1.y);
    q3.y = q1.y + (q2.x - q1.x);
    q3.z = 1.;
    return affine_from_three_points(p1, p2, p3, q1, q2, q3, H);
    /*
     there is probably a better solution.
     we have to solve for H in
                  [x1 x2]
     [ h1 -h2 h3] [y1 y2]   [x1' x2']
     [ h2  h1 h4] [ 1  1] = [y1' y2']

     which is equivalent to
     [x1 -y1 1 0] [h1]   [x1']
     [x2 -y2 1 0] [h2]   [x2']
     [y1  x1 0 1] [h3] = [y1']
     [y2  x2 0 1] [h4]   [y2']
     The 4x4 matrix should be easily invertible
     
     with(linalg);
     M := Matrix([[x1, -y1, 1, 0], [x2, -y2, 1, 0], [y1, x1, 0, 1], [y2, x2, 0, 1]]);
     inverse(M);
     */
    /*
    double det = p1.x*p1.x - 2*p2.x*p1.x + p2.x*p2.x +p1.y*p1.y -2*p1.y*p2.y +p2.y*p2.y;
    if (det == 0.) {
        return false;
    }
    double h1 = (p1.x-p2.x)*(q1.x-q2.x) + (p1.y-p2.y)*(q1.y-q2.y);
    double h2 = (p1.x-p2.x)*(q1.y-q2.y) - (p1.y-p2.y)*(q1.x-q2.x);
    double h3 =
     todo...
     */
}



inline bool
translation_from_one_point(const OFX::Point3D &p1,
                           const OFX::Point3D &q1,
                           OFX::Matrix3x3 *H)
{
    H->a = 1.;
    H->b = 0.;
    H->c = q1.x - p1.x;
    H->d = 0.;
    H->e = 1.;
    H->f = q1.y - p1.y;
    H->g = 0.;
    H->h = 0.;
    H->i = 1.;
    return true;
}


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class CornerPinPlugin : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    CornerPinPlugin(OfxImageEffectHandle handle, bool masked)
    : Transform3x3Plugin(handle, masked, false)
    , _extraMatrixRow1(0)
    , _extraMatrixRow2(0)
    , _extraMatrixRow3(0)
    , _copyFromButton(0)
    , _copyToButton(0)
    , _copyInputButton(0)
    {
        // NON-GENERIC
        for (int i = 0; i < 4; ++i) {
            _to[i] = fetchDouble2DParam(kParamTo[i]);
            _enable[i] = fetchBooleanParam(kParamEnable[i]);
            _from[i] = fetchDouble2DParam(kParamFrom[i]);
            assert(_to[i] && _enable[i] && _from[i]);
        }

        _extraMatrixRow1 = fetchDouble3DParam(kParamExtraMatrixRow1);
        _extraMatrixRow2 = fetchDouble3DParam(kParamExtraMatrixRow2);
        _extraMatrixRow3 = fetchDouble3DParam(kParamExtraMatrixRow3);
        assert(_extraMatrixRow1 && _extraMatrixRow2 && _extraMatrixRow3);

        _copyFromButton = fetchPushButtonParam(kParamCopyFrom);
        _copyToButton = fetchPushButtonParam(kParamCopyTo);
        _copyInputButton = fetchPushButtonParam(kParamCopyInputRoD);
        assert(_copyInputButton && _copyToButton && _copyFromButton);
    }
private:
    
    OFX::Matrix3x3 getExtraMatrix(OfxTime time) const
    {
        OFX::Matrix3x3 ret;
        _extraMatrixRow1->getValueAtTime(time,ret.a, ret.b, ret.c);
        _extraMatrixRow2->getValueAtTime(time,ret.d, ret.e, ret.f);
        _extraMatrixRow3->getValueAtTime(time,ret.g, ret.h, ret.i);
        return ret;
    }
    
    bool getHomography(OfxTime time,const OfxPointD& scale,
                       bool inverseTransform,
                       const OFX::Point3D& p1,
                       const OFX::Point3D& p2,
                       const OFX::Point3D& p3,
                       const OFX::Point3D& p4,
                       OFX::Matrix3x3& m);



    virtual bool isIdentity(double time) OVERRIDE FINAL;

    virtual bool getInverseTransformCanonical(double time, double amount, bool invert, OFX::Matrix3x3* invtransform) const OVERRIDE FINAL;
    
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    //virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    // NON-GENERIC
    OFX::Double2DParam* _to[4];
    OFX::BooleanParam* _enable[4];
    OFX::Double3DParam* _extraMatrixRow1;
    OFX::Double3DParam* _extraMatrixRow2;
    OFX::Double3DParam* _extraMatrixRow3;
    OFX::Double2DParam* _from[4];
    
    OFX::PushButtonParam* _copyFromButton;
    OFX::PushButtonParam* _copyToButton;
    OFX::PushButtonParam* _copyInputButton;
};


bool CornerPinPlugin::getInverseTransformCanonical(OfxTime time, double amount, bool invert, OFX::Matrix3x3* invtransform) const
{
    // in this new version, both from and to are enableds/disabled at the same time
    bool enable[4];
    OFX::Point3D p[2][4];
    int f = invert ? 0 : 1;
    int t = invert ? 1 : 0;
    int k = 0;

    for (int i=0; i < 4; ++i) {
        _enable[i]->getValueAtTime(time, enable[i]);
        if (enable[i]) {
            _from[i]->getValueAtTime(time, p[f][k].x, p[f][k].y);
            _to[i]->getValueAtTime(time, p[t][k].x, p[t][k].y);
            ++k;
        }
        p[0][i].z = p[1][i].z = 1.;
    }

    if (amount != 1.) {
        int k = 0;
        for (int i=0; i < 4; ++i) {
            if (enable[i]) {
                p[t][k].x = p[f][k].x + amount * (p[t][k].x - p[f][k].x);
                p[t][k].y = p[f][k].y + amount * (p[t][k].y - p[f][k].y);
                ++k;
            }
        }
    }

    // k contains the number of valid points
    OFX::Matrix3x3 homo3x3;
    bool success = false;

    assert(0 <= k && k <= 4);
    if (k == 0) {
        // no points, only apply extraMat;
        *invtransform = getExtraMatrix(time);
        return true;
    }

    switch (k) {
        case 4:
            success = homography_from_four_points(p[0][0], p[0][1], p[0][2], p[0][3], p[1][0], p[1][1], p[1][2], p[1][3], &homo3x3);
            break;
        case 3:
            success = affine_from_three_points(p[0][0], p[0][1], p[0][2], p[1][0], p[1][1], p[1][2], &homo3x3);
            break;
        case 2:
            success = similarity_from_two_points(p[0][0], p[0][1], p[1][0], p[1][1], &homo3x3);
            break;
        case 1:
            success = translation_from_one_point(p[0][0], p[1][0], &homo3x3);
            break;
    }
    if (!success) {
        ///cannot compute the homography when 3 points are aligned
        return false;
    }

    OFX::Matrix3x3 extraMat = getExtraMatrix(time);
    *invtransform = homo3x3 * extraMat;

    return true;
}


// overridden is identity
bool CornerPinPlugin::isIdentity(double time)
{
    OFX::Matrix3x3 extraMat = getExtraMatrix(time);
    if (!extraMat.isIdentity()) {
        return false;
    }

    // extraMat is identity.
    
    // The transform is identity either if no point is enabled, or if
    // all enabled from's are equal to their counterpart to
    for (int i = 0; i < 4; ++i) {
        bool enable;
        _enable[i]->getValueAtTime(time, enable);
        if (enable) {
            OfxPointD p, q;
            _from[i]->getValueAtTime(time, p.x, p.y);
            _to[i]->getValueAtTime(time, q.x, q.y);
            if ((p.x != q.x) || (p.y != q.y)) {
                return false;
            }
        }
    }

    return true;
}

static void copyPoint(OFX::Double2DParam* from, OFX::Double2DParam* to)
{
//    OfxPointD p;
//    unsigned int keyCount = from->getNumKeys();
//    to->deleteAllKeys();
//    for (unsigned int i = 0; i < keyCount; ++i) {
//        OfxTime time = from->getKeyTime(i);
//        from->getValueAtTime(time, p.x, p.y);
//        to->setValueAtTime(time, p.x, p.y);
//    }
//    if (keyCount == 0) {
//        from->getValueAtTime(time, p.x, p.y);
//        to->setValue(p.x, p.y);
//    }
    to->copyFrom(*from, 0, NULL);
}

void CornerPinPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamCopyInputRoD) {
        if (_srcClip) {
            const OfxRectD& srcRoD = _srcClip->getRegionOfDefinition(args.time);
            beginEditBlock(kParamCopyInputRoD);
            _from[0]->setValue(srcRoD.x1, srcRoD.y1);
            _from[1]->setValue(srcRoD.x2, srcRoD.y1);
            _from[2]->setValue(srcRoD.x2, srcRoD.y2);
            _from[3]->setValue(srcRoD.x1, srcRoD.y2);
            endEditBlock();
            changedTransform(args);
        }
    } else if (paramName == kParamCopyFrom) {
        beginEditBlock(kParamCopyFrom);
        for (int i=0; i<4; ++i) {
            copyPoint(_from[i],_to[i]);
        }
        endEditBlock();
        changedTransform(args);
    } else if (paramName == kParamCopyTo) {
        beginEditBlock(kParamCopyTo);
        for (int i=0; i<4; ++i) {
            copyPoint(_to[i],_from[i]);
        }
        endEditBlock();
        changedTransform(args);
    } else if (paramName == kParamTo[0] ||
               paramName == kParamTo[1] ||
               paramName == kParamTo[2] ||
               paramName == kParamTo[3] ||
               paramName == kParamEnable[0] ||
               paramName == kParamEnable[1] ||
               paramName == kParamEnable[2] ||
               paramName == kParamEnable[3] ||
               paramName == kParamFrom[0] ||
               paramName == kParamFrom[1] ||
               paramName == kParamFrom[2] ||
               paramName == kParamFrom[3] ||
               paramName == kParamExtraMatrixRow1 ||
               paramName == kParamExtraMatrixRow2 ||
               paramName == kParamExtraMatrixRow3) {
        changedTransform(args);
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
}

//void
//CornerPinPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
//{
    ///Commented-out because if the corner pin is used as a Tracker export from Natron we want the "From" points to stay the same.
    ///Preventing the call to this function in Natron is really messy and quite inapropriate (because we have to differentiate "regular"
    ///CornerPin nodes from "Exported" ones.) Imho the best is to just do nothing here.
//    if (clipName == kOfxImageEffectSimpleSourceClipName && _srcClip && args.reason == OFX::eChangeUserEdit) {
//        const OfxRectD& srcRoD = _srcClip->getRegionOfDefinition(args.time);
//        beginEditBlock(kParamCopyInputRoD);
//        _from[0]->setValue(srcRoD.x1, srcRoD.y1);
//        _from[1]->setValue(srcRoD.x2, srcRoD.y1);
//        _from[2]->setValue(srcRoD.x2, srcRoD.y2);
//        _from[3]->setValue(srcRoD.x1, srcRoD.y2);
//        endEditBlock();
//        changedTransform(args);
//    }
//}

class CornerPinTransformInteract : public OFX::OverlayInteract
{
public:
    
    CornerPinTransformInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _plugin(dynamic_cast<CornerPinPlugin*>(effect))
    , _invert(0)
    , _overlayPoints(0)
    , _interactive(0)
    , _dragging(-1)
    , _hovering(-1)
    , _lastMousePos()
    {
        assert(_plugin);
        for (int i = 0; i < 4; ++i) {
            _to[i] = _plugin->fetchDouble2DParam(kParamTo[i]);
            _from[i] = _plugin->fetchDouble2DParam(kParamFrom[i]);
            _enable[i] = _plugin->fetchBooleanParam(kParamEnable[i]);
            assert(_to[i] && _from[i] && _enable[i]);
            addParamToSlaveTo(_to[i]);
            addParamToSlaveTo(_from[i]);
            addParamToSlaveTo(_enable[i]);
        }
        _invert = _plugin->fetchBooleanParam(kParamTransform3x3Invert);
        addParamToSlaveTo(_invert);
        _overlayPoints = _plugin->fetchChoiceParam(kParamOverlayPoints);
        addParamToSlaveTo(_overlayPoints);
        _interactive = _plugin->fetchBooleanParam(kParamTransformInteractive);
        assert(_invert && _overlayPoints && _interactive);

        for (int i = 0; i < 4; ++i) {
            _toDrag[i].x = _toDrag[i].y = 0;
            _fromDrag[i].x = _fromDrag[i].y = 0;
            _enableDrag[i] = false;
        }
        _useFromDrag = false;
        _interactiveDrag = false;
    }

    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args);
    virtual bool penMotion(const OFX::PenArgs &args);
    virtual bool penDown(const OFX::PenArgs &args);
    virtual bool penUp(const OFX::PenArgs &args);
    //virtual bool keyDown(const OFX::KeyArgs &args);
    //virtual bool keyUp(const OFX::KeyArgs &args);

private:
    
    /**
     * @brief Returns true if the points that should be used by the overlay are
     * the "from" points, otherwise the overlay is assumed to use the "to" points.
     **/
    /*
    bool isFromPoints(double time) const
    {
        int v;
        _overlayPoints->getValueAtTime(time, v);
        return v == 1;
    }
*/
    CornerPinPlugin* _plugin;
    OFX::Double2DParam* _to[4];
    OFX::Double2DParam* _from[4];
    OFX::BooleanParam* _enable[4];
    OFX::BooleanParam* _invert;
    OFX::ChoiceParam* _overlayPoints;
    OFX::BooleanParam* _interactive;

    int _dragging; // -1: idle, else dragging point number
    int _hovering; // -1: idle, else hovering point number
    OfxPointD _lastMousePos;

    OfxPointD _toDrag[4];
    OfxPointD _fromDrag[4];
    bool _enableDrag[4];
    bool _useFromDrag;
    bool _interactiveDrag;
};




static bool isNearby(const OfxPointD& p, double x, double y, double tolerance, const OfxPointD& pscale)
{
    return std::fabs(p.x-x) <= tolerance*pscale.x &&  std::fabs(p.y-y) <= tolerance*pscale.y;
}

bool CornerPinTransformInteract::draw(const OFX::DrawArgs &args)
{
    //const OfxPointD &pscale = args.pixelScale;
    const double &time = args.time;
    OfxRGBColourD color = { 0.8, 0.8, 0.8 };
    getSuggestedColour(color);
    GLdouble projection[16];
    glGetDoublev( GL_PROJECTION_MATRIX, projection);
    OfxPointD shadow; // how much to translate GL_PROJECTION to get exactly one pixel on screen
    shadow.x = 2./(projection[0] * args.viewportSize.x);
    shadow.y = 2./(projection[5] * args.viewportSize.y);

    OfxPointD to[4];
    OfxPointD from[4];
    bool enable[4];
    bool useFrom;

    if (_dragging == -1) {
        for (int i = 0; i < 4; ++i) {
            _to[i]->getValueAtTime(time, to[i].x, to[i].y);
            _from[i]->getValueAtTime(time, from[i].x, from[i].y);
            _enable[i]->getValueAtTime(time, enable[i]);
        }
        int v;
        _overlayPoints->getValueAtTime(time, v);
        useFrom = (v == 1);
    } else {
        for (int i = 0; i < 4; ++i) {
            to[i] = _toDrag[i];
            from[i] = _fromDrag[i];
            enable[i] = _enableDrag[i];
        }
        useFrom = _useFromDrag;
    }

    OfxPointD p[4];
    OfxPointD q[4];
    int enableBegin = 4;
    int enableEnd = 0;
    for (int i = 0; i < 4; ++i) {
        if (enable[i]) {
            if (useFrom) {
                p[i] = from[i];
                q[i] = to[i];
            } else {
                q[i] = from[i];
                p[i] = to[i];
            }
            if (i < enableBegin) {
                enableBegin = i;
            }
            if (i + 1 > enableEnd) {
                enableEnd = i + 1;
            }
        }
    }

    //glPushAttrib(GL_ALL_ATTRIB_BITS); // caller is responsible for protecting attribs

    //glDisable(GL_LINE_STIPPLE);
    glEnable(GL_LINE_SMOOTH);
    //glEnable(GL_POINT_SMOOTH);
    glEnable(GL_BLEND);
    glHint(GL_LINE_SMOOTH_HINT,GL_DONT_CARE);
    glLineWidth(1.5f);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    glPointSize(POINT_SIZE);
    // Draw everything twice
    // l = 0: shadow
    // l = 1: drawing
    for (int l = 0; l < 2; ++l) {
        // shadow (uses GL_PROJECTION)
        glMatrixMode(GL_PROJECTION);
        int direction = (l == 0) ? 1 : -1;
        // translate (1,-1) pixels
        glTranslated(direction * shadow.x, -direction * shadow.y, 0);
        glMatrixMode(GL_MODELVIEW); // Modelview should be used on Nuke

        glColor3f((float)(color.r/2)*l, (float)(color.g/2)*l, (float)(color.b/2)*l);
        glBegin(GL_LINES);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                glVertex2d(p[i].x, p[i].y);
                glVertex2d(q[i].x, q[i].y);
            }
        }
        glEnd();
        glColor3f((float)color.r*l, (float)color.g*l, (float)color.b*l);
        glBegin(GL_LINE_LOOP);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                glVertex2d(p[i].x, p[i].y);
            }
        }
        glEnd();
        glBegin(GL_POINTS);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                if (_hovering == i || _dragging == i) {
                    glColor3f(0.f*l, 1.f*l, 0.f*l);
                } else {
                    glColor3f((float)color.r*l, (float)color.g*l, (float)color.b*l);
                }
                glVertex2d(p[i].x, p[i].y);
            }
        }
        glEnd();
        glColor3f((float)color.r*l, (float)color.g*l, (float)color.b*l);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                TextRenderer::bitmapString(p[i].x, p[i].y, useFrom ? kParamFrom[i] : kParamTo[i]);
            }
        }
    }

    //glPopAttrib();

    return true;
}

bool CornerPinTransformInteract::penMotion(const OFX::PenArgs &args)
{
    const OfxPointD &pscale = args.pixelScale;
    const double &time = args.time;

    OfxPointD to[4];
    OfxPointD from[4];
    bool enable[4];
    bool useFrom;

    if (_dragging == -1) { // mouse is released
        for (int i = 0; i < 4; ++i) {
            _to[i]->getValueAtTime(time, to[i].x, to[i].y);
            _from[i]->getValueAtTime(time, from[i].x, from[i].y);
            _enable[i]->getValueAtTime(time, enable[i]);
        }
        int v;
        _overlayPoints->getValueAtTime(time, v);
        useFrom = (v == 1);
    } else {
        for (int i = 0; i < 4; ++i) {
            to[i] = _toDrag[i];
            from[i] = _fromDrag[i];
            enable[i] = _enableDrag[i];
        }
        useFrom = _useFromDrag;
    }

    OfxPointD p[4];
    //OfxPointD q[4];
    int enableBegin = 4;
    int enableEnd = 0;
    for (int i = 0; i < 4; ++i) {
        if (enable[i]) {
            if (useFrom) {
                p[i] = from[i];
                //q[i] = to[i];
            } else {
                //q[i] = from[i];
                p[i] = to[i];
            }
            if (i < enableBegin) {
                enableBegin = i;
            }
            if (i + 1 > enableEnd) {
                enableEnd = i + 1;
            }
        }
    }

    bool didSomething = false;
    bool valuesChanged = false;
    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;

    _hovering = -1;

    for (int i = enableBegin; i < enableEnd; ++i) {
        if (enable[i]) {
            if (_dragging == i) {
                if (useFrom) {
                    from[i].x += delta.x;
                    from[i].y += delta.y;
                    _fromDrag[i] = from[i];
                } else {
                    to[i].x += delta.x;
                    to[i].y += delta.y;
                    _toDrag[i] = to[i];
                }
                valuesChanged = true;
            } else if (isNearby(args.penPosition, p[i].x, p[i].y, POINT_TOLERANCE, pscale)) {
                _hovering = i;
                didSomething = true;
            }
        }
    }

    if (_dragging != -1 && _interactiveDrag && valuesChanged) {
        // no need to redraw overlay since it is slave to the paramaters
        if (useFrom) {
            _from[_dragging]->setValue(from[_dragging].x, from[_dragging].y);
        } else {
            _to[_dragging]->setValue(to[_dragging].x, to[_dragging].y);
        }
    } else if (didSomething || valuesChanged) {
        _effect->redrawOverlays();
    }

    _lastMousePos = args.penPosition;
    
    return didSomething || valuesChanged;
}

bool CornerPinTransformInteract::penDown(const OFX::PenArgs &args)
{
    const OfxPointD &pscale = args.pixelScale;
    const double &time = args.time;

    OfxPointD to[4];
    OfxPointD from[4];
    bool enable[4];
    bool useFrom;

    if (_dragging == -1) {
        for (int i = 0; i < 4; ++i) {
            _to[i]->getValueAtTime(time, to[i].x, to[i].y);
            _from[i]->getValueAtTime(time, from[i].x, from[i].y);
            _enable[i]->getValueAtTime(time, enable[i]);
        }
        int v;
        _overlayPoints->getValueAtTime(time, v);
        useFrom = (v == 1);
        if (_interactive) {
            _interactive->getValueAtTime(args.time, _interactiveDrag);
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            to[i] = _toDrag[i];
            from[i] = _fromDrag[i];
            enable[i] = _enableDrag[i];
        }
        useFrom = _useFromDrag;
    }

    OfxPointD p[4];
    //OfxPointD q[4];
    int enableBegin = 4;
    int enableEnd = 0;
    for (int i = 0; i < 4; ++i) {
        if (enable[i]) {
            if (useFrom) {
                p[i] = from[i];
                //q[i] = to[i];
            } else {
                //q[i] = from[i];
                p[i] = to[i];
            }
            if (i < enableBegin) {
                enableBegin = i;
            }
            if (i + 1 > enableEnd) {
                enableEnd = i + 1;
            }
        }
    }

    bool didSomething = false;

    for (int i = enableBegin; i < enableEnd; ++i) {
        if (enable[i]) {
            if (isNearby(args.penPosition, p[i].x, p[i].y, POINT_TOLERANCE, pscale)) {
                _dragging = i;
                didSomething = true;
            }
            _toDrag[i] = to[i];
            _fromDrag[i] = from[i];
            _enableDrag[i] = enable[i];
        }
    }
    _useFromDrag = useFrom;

    if (didSomething) {
    _effect->redrawOverlays();
    }

    _lastMousePos = args.penPosition;

    return didSomething;
}

bool CornerPinTransformInteract::penUp(const OFX::PenArgs &/*args*/)
{
    bool didSomething = _dragging != -1;

    if (!_interactiveDrag && _dragging != -1) {
        // no need to redraw overlay since it is slave to the paramaters
        if (_useFromDrag) {
            _from[_dragging]->setValue(_fromDrag[_dragging].x, _fromDrag[_dragging].y);
        } else {
            _to[_dragging]->setValue(_toDrag[_dragging].x, _toDrag[_dragging].y);
        }
    } else if (didSomething) {
        _effect->redrawOverlays();
    }

    _dragging = -1;

    return didSomething;
}

using namespace OFX;

class CornerPinOverlayDescriptor : public DefaultEffectOverlayDescriptor<CornerPinOverlayDescriptor, CornerPinTransformInteract> {};

static void defineCornerPinToDouble2DParam(OFX::ImageEffectDescriptor &desc,
                                           PageParamDescriptor *page,
                                           GroupParamDescriptor* group,
                                           int i,
                                           double x,
                                           double y)
{
    // size
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamTo[i]);
        param->setLabel(kParamTo[i]);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setAnimates(true);
        param->setDefault(x, y);
        param->setIncrement(1.);
        param->setDimensionLabels("x", "y");
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        param->setParent(*group);
        if (page) {
            page->addChild(*param);
        }
    }

    // enable
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamEnable[i]);
        param->setLabel(kParamEnable[i]);
        param->setDefault(true);
        param->setAnimates(true);
        param->setHint(kParamEnableHint);
        param->setParent(*group);
        if (page) {
            page->addChild(*param);
        }
    }
}

static void defineCornerPinFromsDouble2DParam(OFX::ImageEffectDescriptor &desc,
                                              PageParamDescriptor *page,
                                              GroupParamDescriptor* group,
                                              int i,
                                              double x,
                                              double y)
{
    Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamFrom[i]);
    param->setLabel(kParamFrom[i]);
    param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
    param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
    param->setAnimates(true);
    param->setDefault(x, y);
    param->setIncrement(1.);
    param->setDimensionLabels("x", "y");
    param->setParent(*group);
    if (page) {
        page->addChild(*param);
    }
}

static void defineExtraMatrixRow(OFX::ImageEffectDescriptor &desc,
                                 PageParamDescriptor *page,
                                 GroupParamDescriptor* group,
                                 const std::string& name,
                                 double x,
                                 double y,
                                 double z)
{
    Double3DParamDescriptor* param = desc.defineDouble3DParam(name);
    param->setLabel("");
    param->setAnimates(true);
    param->setDefault(x,y,z);
    param->setIncrement(0.01);
    param->setParent(*group);
    if (page) {
        page->addChild(*param);
    }
}

static void
CornerPinPluginDescribeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/, PageParamDescriptor *page)
{
    // NON-GENERIC PARAMETERS
    //
    // toPoints
    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupTo);
        group->setLabel(kGroupTo);
        group->setAsTab();

        defineCornerPinToDouble2DParam(desc, page, group, 0, 0, 0);
        defineCornerPinToDouble2DParam(desc, page, group, 1, 1, 0);
        defineCornerPinToDouble2DParam(desc, page, group, 2, 1, 1);
        defineCornerPinToDouble2DParam(desc, page, group, 3, 0, 1);

        // copyFrom
        {
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamCopyFrom);
            param->setLabel(kParamCopyFromLabel);
            param->setHint(kParamCopyFromHint);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }

        if (page) {
            page->addChild(*group);
        }
    }

    // fromPoints
    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupFrom);
        group->setLabel(kGroupFrom);
        group->setAsTab();

        defineCornerPinFromsDouble2DParam(desc, page, group, 0, 0, 0);
        defineCornerPinFromsDouble2DParam(desc, page, group, 1, 1, 0);
        defineCornerPinFromsDouble2DParam(desc, page, group, 2, 1, 1);
        defineCornerPinFromsDouble2DParam(desc, page, group, 3, 0, 1);

        // setToInput
        {
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamCopyInputRoD);
            param->setLabel(kParamCopyInputRoDLabel);
            param->setHint(kParamCopyInputRoDHint);
            param->setLayoutHint(OFX::eLayoutHintNoNewLine);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }

        // copyTo
        {
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamCopyTo);
            param->setLabel(kParamCopyToLabel);
            param->setHint(kParamCopyToHint);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }
        
        if (page) {
            page->addChild(*group);
        }
    }
    
    // extraMatrix
    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupExtraMatrix);
        group->setLabel(kGroupExtraMatrixLabel);
        group->setHint(kGroupExtraMatrixHint);
        group->setOpen(false);

        defineExtraMatrixRow(desc, page, group, kParamExtraMatrixRow1, 1, 0, 0);
        defineExtraMatrixRow(desc, page, group, kParamExtraMatrixRow2, 0, 1, 0);
        defineExtraMatrixRow(desc, page, group, kParamExtraMatrixRow3, 0, 0, 1);

        if (page) {
            page->addChild(*group);
        }
    }

    // overlayPoints
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOverlayPoints);
        param->setLabel(kParamOverlayPointsLabel);
        param->setHint(kParamOverlayPointsHint);
        param->appendOption("To");
        param->appendOption("From");
        param->setDefault(0);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // interactive
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamTransformInteractive);
        param->setLabel(kParamTransformInteractiveLabel);
        param->setHint(kParamTransformInteractiveHint);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

mDeclarePluginFactory(CornerPinPluginFactory, {}, {});

void CornerPinPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, false);

    desc.setOverlayInteractDescriptor(new CornerPinOverlayDescriptor);
}

void CornerPinPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

    CornerPinPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, false);
}


OFX::ImageEffect* CornerPinPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CornerPinPlugin(handle, false);
}




mDeclarePluginFactory(CornerPinMaskedPluginFactory, {}, {});

void CornerPinMaskedPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginMaskedName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, true);

    desc.setOverlayInteractDescriptor(new CornerPinOverlayDescriptor);
}

void CornerPinMaskedPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    CornerPinPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, true);
}

OFX::ImageEffect* CornerPinMaskedPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CornerPinPlugin(handle, true);
}



void getCornerPinPluginIDs(OFX::PluginFactoryArray &ids)
{
    {
        static CornerPinPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static CornerPinMaskedPluginFactory p(kPluginMaskedIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}
