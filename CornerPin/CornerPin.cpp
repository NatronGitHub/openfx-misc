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
// Uncomment the following to enable the experimental host transform code.
//#define ENABLE_HOST_TRANSFORM

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

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

#define kPluginName "CornerPinOFX"
#define kPluginMaskedName "CornerPinMaskedOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription "Allows an image to fit another in translation, rotation and scale."
#define kPluginIdentifier "net.sf.openfx:CornerPinPlugin"
#define kPluginMaskedIdentifier "net.sf.openfx:CornerPinMaskedPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.


#ifndef ENABLE_HOST_TRANSFORM
#undef OFX_EXTENSIONS_NUKE // host transform is the only nuke extension used
#endif

#define kToParamGroupName "to"
#define kToParamGroupLabel "To"
static const char* const kToParamName[4] = {
    "to1",
    "to2",
    "to3",
    "to4"
};

static const char* const kEnableParamName[4] = {
    "enable1",
    "enable2",
    "enable3",
    "enable4"
};
#define kEnableParamHint "Enables the point on the left."

#define kFromParamGroupName "from"
#define kFromParamGroupLabel "From"
static const char* const kFromParamName[4] = {
    "from1",
    "from2",
    "from3",
    "from4"
};


#define kCopyFromParamName "copyFrom"
#define kCopyFromParamLabel "Copy \"From\" points"
#define kCopyFromParamHint "Copy the content from the \"to\" points to the \"from\" points."

#define kCopyToParamName "copyTo"
#define kCopyToParamLabel "Copy \"To\" points"
#define kCopyToParamHint "Copy the content from the \"from\" points to the \"to\" points."

#define kCopyInputRoDParamName "setToInputRod"
#define kCopyInputRoDParamLabel "Set to input rod"
#define kCopyInputRoDParamHint "Copy the values from the source region of definition into the \"to\" points."

#define kOverlayPointsParamName "overlayPoints"
#define kOverlayPointsParamLabel "Overlay points"
#define kOverlayPointsParamHint "Whether to display the \"from\" or the \"to\" points in the overlay"

#define kExtraMatrixParamName "transformMatrix"
#define kExtraMatrixParamLabel "Extra matrix"
#define kExtraMatrixParamHint "This matrix gets concatenated to the transform defined by the other parameters."
#define kExtraMatrixRow1ParamName "row1"
#define kExtraMatrixRow2ParamName "row2"
#define kExtraMatrixRow3ParamName "row3"

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
    : Transform3x3Plugin(handle, masked)
    , _extraMatrixRow1(0)
    , _extraMatrixRow2(0)
    , _extraMatrixRow3(0)
    , _copyFromButton(0)
    , _copyToButton(0)
    , _copyInputButton(0)
    {
        // NON-GENERIC
        for (int i = 0; i < 4; ++i) {
            _to[i] = fetchDouble2DParam(kToParamName[i]);
            _enable[i] = fetchBooleanParam(kEnableParamName[i]);
            _from[i] = fetchDouble2DParam(kFromParamName[i]);
            assert(_to[i] && _enable[i] && _from[i]);
        }

        _extraMatrixRow1 = fetchDouble3DParam(kExtraMatrixRow1ParamName);
        _extraMatrixRow2 = fetchDouble3DParam(kExtraMatrixRow2ParamName);
        _extraMatrixRow3 = fetchDouble3DParam(kExtraMatrixRow3ParamName);
        assert(_extraMatrixRow1 && _extraMatrixRow2 && _extraMatrixRow3);

        _copyFromButton = fetchPushButtonParam(kCopyFromParamName);
        _copyToButton = fetchPushButtonParam(kCopyToParamName);
        _copyInputButton = fetchPushButtonParam(kCopyInputRoDParamName);
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



    virtual bool isIdentity(double time) /*OVERRIDE FINAL*/;

    virtual bool getInverseTransformCanonical(double time, bool invert, OFX::Matrix3x3* invtransform) const /*OVERRIDE FINAL*/;
    
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) /*OVERRIDE FINAL*/;

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


bool CornerPinPlugin::getInverseTransformCanonical(OfxTime time, bool invert, OFX::Matrix3x3* invtransform) const
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

    // all enabled points must be equal
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
    if (paramName == kCopyInputRoDParamName) {
        const OfxRectD srcRoD = srcClip_->getRegionOfDefinition(args.time);
        _from[0]->setValue(srcRoD.x1, srcRoD.y1);
        _from[1]->setValue(srcRoD.x2, srcRoD.y1);
        _from[2]->setValue(srcRoD.x2, srcRoD.y2);
        _from[3]->setValue(srcRoD.x1, srcRoD.y2);
        changedTransform(args);
    } else if (paramName == kCopyFromParamName) {
        for (int i=0; i<4; ++i) {
            copyPoint(_from[i],_to[i]);
        }
        changedTransform(args);
    } else if (paramName == kCopyToParamName) {
        for (int i=0; i<4; ++i) {
            copyPoint(_to[i],_from[i]);
        }
        changedTransform(args);
    } else if (paramName == kToParamName[0] ||
               paramName == kToParamName[1] ||
               paramName == kToParamName[2] ||
               paramName == kToParamName[3] ||
               paramName == kEnableParamName[0] ||
               paramName == kEnableParamName[1] ||
               paramName == kEnableParamName[2] ||
               paramName == kEnableParamName[3] ||
               paramName == kFromParamName[0] ||
               paramName == kFromParamName[1] ||
               paramName == kFromParamName[2] ||
               paramName == kFromParamName[3] ||
               paramName == kExtraMatrixRow1ParamName ||
               paramName == kExtraMatrixRow2ParamName ||
               paramName == kExtraMatrixRow3ParamName) {
        changedTransform(args);
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
}


class CornerPinTransformInteract : public OFX::OverlayInteract
{
public:
    
    CornerPinTransformInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _invert(0)
    , _overlayChoice(0)
    , _dragging(-1)
    , _hovering(-1)
    , _lastMousePos()
    , _lastPenDownPos()
    {
        for (int i = 0; i < 4; ++i) {
            _to[i] = effect->fetchDouble2DParam(kToParamName[i]);
            _from[i] = effect->fetchDouble2DParam(kFromParamName[i]);
            assert(_to[i] && _from[i]);
            addParamToSlaveTo(_to[i]);
            addParamToSlaveTo(_from[i]);
        }
        _invert = effect->fetchBooleanParam(kTransform3x3InvertParamName);
        addParamToSlaveTo(_invert);
        _overlayChoice = effect->fetchChoiceParam(kOverlayPointsParamName);
        addParamToSlaveTo(_overlayChoice);
    }

    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args);
    virtual bool penMotion(const OFX::PenArgs &args);
    virtual bool penDown(const OFX::PenArgs &args);
    virtual bool penUp(const OFX::PenArgs &args);
    virtual bool keyDown(const OFX::KeyArgs &args);
    virtual bool keyUp(const OFX::KeyArgs &args);

private:
    
    /**
     * @brief Returns true if the points that should be used by the overlay are
     * the "from" points, otherwise the overlay is assumed to use the "to" points.
     **/
    bool isFromPoints(double time) const
    {
        int v;
        _overlayChoice->getValueAtTime(time, v);
        return v == 1;
    }
    
    bool isNearbyTo(double time, int i, const OfxPointD& pos,bool useFromPoints,double tolerance) const;

    OFX::Double2DParam* _to[4];
    OFX::Double2DParam* _from[4];
    OFX::BooleanParam* _invert;
    OFX::ChoiceParam* _overlayChoice;
    
    int _dragging; // -1: idle, else dragging point number
    int _hovering; // -1: idle, else hovering point number
    OfxPointD _lastMousePos;
    OfxPointD _lastPenDownPos;
    
    OfxPointD _draggedPos[4];
};




bool CornerPinTransformInteract::isNearbyTo(double time, int i, const OfxPointD& pos,bool useFromPoints,double tolerance) const
{
    OfxPointD p;
    useFromPoints ? _from[i]->getValueAtTime(time, p.x, p.y) :_to[i]->getValueAtTime(time, p.x, p.y);
    return std::fabs(pos.x-p.x) < tolerance && std::fabs(pos.y-p.y) < tolerance;
}

bool CornerPinTransformInteract::draw(const OFX::DrawArgs &args)
{
    bool useFrom = isFromPoints(args.time);

    OfxPointD p[4];
    for (int i = 0; i < 4; ++i) {
        if (_dragging == i) {
            p[i] = _draggedPos[i];
        } else {
            useFrom ? _from[i]->getValueAtTime(args.time, p[i].x, p[i].y) :_to[i]->getValueAtTime(args.time, p[i].x, p[i].y);
        }
    }
    
    glPushAttrib(GL_POINT_BIT);
    glPointSize(5);
    glBegin(GL_POINTS);
    for (int i = 0; i < 4; ++i) {
        if (_hovering == i || _dragging == i) {
            glColor4f(0., 1., 0., 1.);
        } else {
            glColor4f(1., 1., 1., 1.);
        }
        glVertex2d(p[i].x, p[i].y);
    }
    glEnd();

    for (int i = 0; i < 4; ++i) {
        TextRenderer::bitmapString(p[i].x, p[i].y, useFrom ? kFromParamName[i] : kToParamName[i]);
    }

    glPopAttrib();

    return true;
}

bool CornerPinTransformInteract::penMotion(const OFX::PenArgs &args)
{

    bool didSomething = false;
    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;
    
    bool useFrom = isFromPoints(args.time);
    
    double selectionTol = 15. * args.pixelScale.x;
    _hovering = -1;
    didSomething = false;

    for (int i = 0; i < 4; ++i) {
        if (_dragging == i) {
            _draggedPos[i].x += delta.x;
            _draggedPos[i].y += delta.y;
            didSomething = true;
        } else if (isNearbyTo(args.time, i, args.penPosition, useFrom, selectionTol)) {
            _hovering = i;
            didSomething = true;
        }
    }

    _lastMousePos = args.penPosition;
    return didSomething;
}

bool CornerPinTransformInteract::penDown(const OFX::PenArgs &args)
{
    bool didSomething = false;
    
    double selectionTol = 15. * args.pixelScale.x;
    bool useFrom = isFromPoints(args.time);

    for (int i = 0; i < 4; ++i) {
        if (isNearbyTo(args.time, i, args.penPosition, useFrom, selectionTol)) {
            _dragging = i;
            _draggedPos[i] = args.penPosition;
            didSomething = true;
        }
    }

    _lastMousePos = args.penPosition;
    return didSomething;
}

bool CornerPinTransformInteract::penUp(const OFX::PenArgs &args)
{
    bool didSomething = false;

    bool useFrom = isFromPoints(args.time);

    for (int i = 0; i < 4; ++i) {
        if (_dragging == i) {
            useFrom ? _from[i]->setValue(_draggedPos[i].x, _draggedPos[i].y)
            : _to[i]->setValue(_draggedPos[i].x,_draggedPos[i].y);
            didSomething = true;
        }
    }
    
    _dragging = -1;
    return didSomething;
}

bool CornerPinTransformInteract::keyDown(const OFX::KeyArgs &args)
{
    bool didSomething = false;
    return didSomething;
}

bool CornerPinTransformInteract::keyUp(const OFX::KeyArgs &args)
{
    bool didSomething = false;
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
    Double2DParamDescriptor* size = desc.defineDouble2DParam(kToParamName[i]);
    size->setLabels(kToParamName[i], kToParamName[i], kToParamName[i]);
    size->setDoubleType(OFX::eDoubleTypeXYAbsolute);
    size->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
    size->setAnimates(true);
    size->setDefault(x, y);
    size->setDimensionLabels("x", "y");
    size->setLayoutHint(OFX::eLayoutHintNoNewLine);
    size->setParent(*group);
    page->addChild(*size);

    BooleanParamDescriptor* enable = desc.defineBooleanParam(kEnableParamName[i]);
    enable->setLabels(kEnableParamName[i], kEnableParamName[i], kEnableParamName[i]);
    enable->setDefault(true);
    enable->setAnimates(true);
    enable->setHint(kEnableParamHint);
    enable->setParent(*group);
    page->addChild(*enable);
}

static void defineCornerPinFromsDouble2DParam(OFX::ImageEffectDescriptor &desc,
                                              PageParamDescriptor *page,
                                              GroupParamDescriptor* group,
                                              int i,
                                              double x,
                                              double y)
{
    Double2DParamDescriptor* size = desc.defineDouble2DParam(kFromParamName[i]);
    size->setLabels(kFromParamName[i], kFromParamName[i], kFromParamName[i]);
    size->setDoubleType(OFX::eDoubleTypeXYAbsolute);
    size->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
    size->setAnimates(true);
    size->setDefault(x, y);
    size->setDimensionLabels("x", "y");
    size->setParent(*group);
    page->addChild(*size);
}

static void defineExtraMatrixRow(OFX::ImageEffectDescriptor &desc,
                                 PageParamDescriptor *page,
                                 GroupParamDescriptor* group,
                                 const std::string& name,
                                 double x,
                                 double y,
                                 double z)
{
    Double3DParamDescriptor* row = desc.defineDouble3DParam(name);
    row->setLabels("", "", "");
    row->setAnimates(true);
    row->setDefault(x,y,z);
    row->setParent(*group);
    page->addChild(*row);
}

static void
CornerPinPluginDescribeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, PageParamDescriptor *page)
{
    // NON-GENERIC PARAMETERS
    //
    GroupParamDescriptor* toPoints = desc.defineGroupParam(kToParamGroupName);
    toPoints->setLabels(kToParamGroupName, kToParamGroupName, kToParamGroupName);
    toPoints->setAsTab();
    defineCornerPinToDouble2DParam(desc, page, toPoints, 0, 0, 0);
    defineCornerPinToDouble2DParam(desc, page, toPoints, 1, 1, 0);
    defineCornerPinToDouble2DParam(desc, page, toPoints, 2, 1, 1);
    defineCornerPinToDouble2DParam(desc, page, toPoints, 3, 0, 1);
    
    PushButtonParamDescriptor* copyFrom = desc.definePushButtonParam(kCopyFromParamName);
    copyFrom->setLabels(kCopyFromParamLabel, kCopyFromParamLabel, kCopyFromParamLabel);
    copyFrom->setHint(kCopyFromParamHint);
    copyFrom->setParent(*toPoints);
    page->addChild(*copyFrom);
    
    page->addChild(*toPoints);

    GroupParamDescriptor* fromPoints = desc.defineGroupParam(kFromParamGroupName);
    fromPoints->setLabels(kFromParamGroupName, kFromParamGroupName, kFromParamGroupName);
    fromPoints->setAsTab();
    defineCornerPinFromsDouble2DParam(desc, page, fromPoints, 0, 0, 0);
    defineCornerPinFromsDouble2DParam(desc, page, fromPoints, 1, 1, 0);
    defineCornerPinFromsDouble2DParam(desc, page, fromPoints, 2, 1, 1);
    defineCornerPinFromsDouble2DParam(desc, page, fromPoints, 3, 0, 1);
    
    PushButtonParamDescriptor* setToInput = desc.definePushButtonParam(kCopyInputRoDParamName);
    setToInput->setLabels(kCopyInputRoDParamLabel, kCopyInputRoDParamLabel, kCopyInputRoDParamLabel);
    setToInput->setHint(kCopyInputRoDParamHint);
    setToInput->setLayoutHint(OFX::eLayoutHintNoNewLine);
    setToInput->setParent(*fromPoints);
    page->addChild(*setToInput);
    
    PushButtonParamDescriptor* copyTo = desc.definePushButtonParam(kCopyToParamName);
    copyTo->setLabels(kCopyToParamLabel, kCopyToParamLabel, kCopyToParamLabel);
    copyTo->setHint(kCopyToParamHint);
    copyTo->setParent(*fromPoints);
    page->addChild(*copyTo);
    
    page->addChild(*fromPoints);

    
    GroupParamDescriptor* extraMatrix = desc.defineGroupParam(kExtraMatrixParamName);
    extraMatrix->setLabels(kExtraMatrixParamLabel, kExtraMatrixParamLabel, kExtraMatrixParamLabel);
    extraMatrix->setHint(kExtraMatrixParamHint);
    extraMatrix->setOpen(false);
    defineExtraMatrixRow(desc, page, extraMatrix,kExtraMatrixRow1ParamName,1,0,0);
    defineExtraMatrixRow(desc, page, extraMatrix,kExtraMatrixRow2ParamName,0,1,0);
    defineExtraMatrixRow(desc, page, extraMatrix,kExtraMatrixRow3ParamName,0,0,1);
    page->addChild(*extraMatrix);

    ChoiceParamDescriptor* overlayChoice = desc.defineChoiceParam(kOverlayPointsParamName);
    overlayChoice->setLabels(kOverlayPointsParamLabel, kOverlayPointsParamLabel, kOverlayPointsParamLabel);
    overlayChoice->setHint(kOverlayPointsParamHint);
    overlayChoice->appendOption("To");
    overlayChoice->appendOption("From");
    overlayChoice->setDefault(0);
    overlayChoice->setAnimates(true);
    page->addChild(*overlayChoice);
}

mDeclarePluginFactory(CornerPinPluginFactory, {}, {});

void CornerPinPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
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


OFX::ImageEffect* CornerPinPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new CornerPinPlugin(handle, false);
}




mDeclarePluginFactory(CornerPinMaskedPluginFactory, {}, {});

void CornerPinMaskedPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginMaskedName, kPluginMaskedName, kPluginMaskedName);
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

OFX::ImageEffect* CornerPinMaskedPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new CornerPinPlugin(handle, true);
}



void getCornerPinPluginID(OFX::PluginFactoryArray &ids)
{
    static CornerPinPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

void getCornerPinMaskedPluginID(OFX::PluginFactoryArray &ids)
{
    static CornerPinMaskedPluginFactory p(kPluginMaskedIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

