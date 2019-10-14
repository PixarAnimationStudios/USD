//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#ifndef USDGEOM_TOKENS_H
#define USDGEOM_TOKENS_H

/// \file usdGeom/tokens.h

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// 
// This is an automatically generated file (by usdGenSchema.py).
// Do not hand-edit!
// 
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#include "pxr/pxr.h"
#include "pxr/usd/usdGeom/api.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/token.h"
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE


/// \class UsdGeomTokensType
///
/// \link UsdGeomTokens \endlink provides static, efficient
/// \link TfToken TfTokens\endlink for use in all public USD API.
///
/// These tokens are auto-generated from the module's schema, representing
/// property names, for when you need to fetch an attribute or relationship
/// directly by name, e.g. UsdPrim::GetAttribute(), in the most efficient
/// manner, and allow the compiler to verify that you spelled the name
/// correctly.
///
/// UsdGeomTokens also contains all of the \em allowedTokens values
/// declared for schema builtin attributes of 'token' scene description type.
/// Use UsdGeomTokens like so:
///
/// \code
///     gprim.GetMyTokenValuedAttr().Set(UsdGeomTokens->accelerations);
/// \endcode
struct UsdGeomTokensType {
    USDGEOM_API UsdGeomTokensType();
    /// \brief "accelerations"
    /// 
    /// UsdGeomPointInstancer, UsdGeomPointBased
    const TfToken accelerations;
    /// \brief "all"
    /// 
    /// Possible value for UsdGeomMesh::GetFaceVaryingLinearInterpolationAttr()
    const TfToken all;
    /// \brief "angularVelocities"
    /// 
    /// UsdGeomPointInstancer
    const TfToken angularVelocities;
    /// \brief "axis"
    /// 
    /// UsdGeomPlane, UsdGeomCone, UsdGeomCapsule, UsdGeomCylinder
    const TfToken axis;
    /// \brief "basis"
    /// 
    /// UsdGeomBasisCurves
    const TfToken basis;
    /// \brief "bezier"
    /// 
    /// Possible value for UsdGeomBasisCurves::GetBasisAttr(), Default value for UsdGeomBasisCurves::GetBasisAttr()
    const TfToken bezier;
    /// \brief "bilinear"
    /// 
    /// Possible value for UsdGeomMesh::GetSubdivisionSchemeAttr()
    const TfToken bilinear;
    /// \brief "boundaries"
    /// 
    /// Possible value for UsdGeomMesh::GetFaceVaryingLinearInterpolationAttr()
    const TfToken boundaries;
    /// \brief "bounds"
    /// 
    /// Possible value for UsdGeomModelAPI::GetModelDrawModeAttr()
    const TfToken bounds;
    /// \brief "box"
    /// 
    /// Possible value for UsdGeomModelAPI::GetModelCardGeometryAttr()
    const TfToken box;
    /// \brief "bspline"
    /// 
    /// Possible value for UsdGeomBasisCurves::GetBasisAttr()
    const TfToken bspline;
    /// \brief "cards"
    /// 
    /// Possible value for UsdGeomModelAPI::GetModelDrawModeAttr()
    const TfToken cards;
    /// \brief "catmullClark"
    /// 
    /// Possible value for UsdGeomMesh::GetSubdivisionSchemeAttr(), Default value for UsdGeomMesh::GetSubdivisionSchemeAttr(), Possible value for UsdGeomMesh::GetTriangleSubdivisionRuleAttr(), Default value for UsdGeomMesh::GetTriangleSubdivisionRuleAttr()
    const TfToken catmullClark;
    /// \brief "catmullRom"
    /// 
    /// Possible value for UsdGeomBasisCurves::GetBasisAttr()
    const TfToken catmullRom;
    /// \brief "clippingPlanes"
    /// 
    /// UsdGeomCamera
    const TfToken clippingPlanes;
    /// \brief "clippingRange"
    /// 
    /// UsdGeomCamera
    const TfToken clippingRange;
    /// \brief "closed"
    /// 
    /// Possible value for UsdGeomNurbsPatch::GetVFormAttr(), Possible value for UsdGeomNurbsPatch::GetUFormAttr()
    const TfToken closed;
    /// \brief "constant"
    /// 
    /// Possible value for UsdGeomPrimvar::SetInterpolation. Default value for UsdGeomPrimvar::GetInterpolation. One value remains constant over the entire surface primitive.
    const TfToken constant;
    /// \brief "cornerIndices"
    /// 
    /// UsdGeomMesh
    const TfToken cornerIndices;
    /// \brief "cornerSharpnesses"
    /// 
    /// UsdGeomMesh
    const TfToken cornerSharpnesses;
    /// \brief "cornersOnly"
    /// 
    /// Possible value for UsdGeomMesh::GetFaceVaryingLinearInterpolationAttr()
    const TfToken cornersOnly;
    /// \brief "cornersPlus1"
    /// 
    /// Possible value for UsdGeomMesh::GetFaceVaryingLinearInterpolationAttr(), Default value for UsdGeomMesh::GetFaceVaryingLinearInterpolationAttr()
    const TfToken cornersPlus1;
    /// \brief "cornersPlus2"
    /// 
    /// Possible value for UsdGeomMesh::GetFaceVaryingLinearInterpolationAttr()
    const TfToken cornersPlus2;
    /// \brief "creaseIndices"
    /// 
    /// UsdGeomMesh
    const TfToken creaseIndices;
    /// \brief "creaseLengths"
    /// 
    /// UsdGeomMesh
    const TfToken creaseLengths;
    /// \brief "creaseSharpnesses"
    /// 
    /// UsdGeomMesh
    const TfToken creaseSharpnesses;
    /// \brief "cross"
    /// 
    /// Possible value for UsdGeomModelAPI::GetModelCardGeometryAttr()
    const TfToken cross;
    /// \brief "cubic"
    /// 
    /// Possible value for UsdGeomBasisCurves::GetTypeAttr(), Default value for UsdGeomBasisCurves::GetTypeAttr()
    const TfToken cubic;
    /// \brief "curveVertexCounts"
    /// 
    /// UsdGeomCurves
    const TfToken curveVertexCounts;
    /// \brief "default"
    /// 
    /// Possible value for UsdGeomModelAPI::GetModelDrawModeAttr(), Possible value for UsdGeomImageable::GetPurposeAttr(), Default value for UsdGeomImageable::GetPurposeAttr()
    const TfToken default_;
    /// \brief "doubleSided"
    /// 
    /// UsdGeomPlane, UsdGeomGprim
    const TfToken doubleSided;
    /// \brief "edgeAndCorner"
    /// 
    /// Possible value for UsdGeomMesh::GetInterpolateBoundaryAttr(), Default value for UsdGeomMesh::GetInterpolateBoundaryAttr()
    const TfToken edgeAndCorner;
    /// \brief "edgeOnly"
    /// 
    /// Possible value for UsdGeomMesh::GetInterpolateBoundaryAttr()
    const TfToken edgeOnly;
    /// \brief "elementSize"
    /// 
    /// UsdGeomPrimvar - The number of values in the value array that must be aggregated for each element on the  primitive.
    const TfToken elementSize;
    /// \brief "elementType"
    /// 
    /// UsdGeomSubset
    const TfToken elementType;
    /// \brief "extent"
    /// 
    /// UsdGeomPlane, UsdGeomCone, UsdGeomCapsule, UsdGeomCylinder, UsdGeomSphere, UsdGeomCube, UsdGeomBoundable
    const TfToken extent;
    /// \brief "extentsHint"
    /// 
    /// Name of the attribute used to author extents hints at the root of leaf models. Extents hints are stored by purpose as a vector of GfVec3f values. They are ordered based on the order of purpose tokens returned by  UsdGeomImageable::GetOrderedPurposeTokens.
    const TfToken extentsHint;
    /// \brief "face"
    /// 
    /// Possible value for UsdGeomSubset::GetElementTypeAttr(), Default value for UsdGeomSubset::GetElementTypeAttr()
    const TfToken face;
    /// \brief "faceVarying"
    /// 
    /// Possible value for UsdGeomPrimvar::SetInterpolation. For polygons and subdivision surfaces, four values are interpolated over each face of the mesh. Bilinear interpolation  is used for interpolation between the four values.
    const TfToken faceVarying;
    /// \brief "faceVaryingLinearInterpolation"
    /// 
    /// UsdGeomMesh
    const TfToken faceVaryingLinearInterpolation;
    /// \brief "faceVertexCounts"
    /// 
    /// UsdGeomMesh
    const TfToken faceVertexCounts;
    /// \brief "faceVertexIndices"
    /// 
    /// UsdGeomMesh
    const TfToken faceVertexIndices;
    /// \brief "familyName"
    /// 
    /// UsdGeomSubset
    const TfToken familyName;
    /// \brief "focalLength"
    /// 
    /// UsdGeomCamera
    const TfToken focalLength;
    /// \brief "focusDistance"
    /// 
    /// UsdGeomCamera
    const TfToken focusDistance;
    /// \brief "fromTexture"
    /// 
    /// Possible value for UsdGeomModelAPI::GetModelCardGeometryAttr()
    const TfToken fromTexture;
    /// \brief "fStop"
    /// 
    /// UsdGeomCamera
    const TfToken fStop;
    /// \brief "guide"
    /// 
    /// Possible value for UsdGeomImageable::GetPurposeAttr()
    const TfToken guide;
    /// \brief "height"
    /// 
    /// UsdGeomCone, UsdGeomCapsule, UsdGeomCylinder
    const TfToken height;
    /// \brief "hermite"
    /// 
    /// Possible value for UsdGeomBasisCurves::GetBasisAttr()
    const TfToken hermite;
    /// \brief "holeIndices"
    /// 
    /// UsdGeomMesh
    const TfToken holeIndices;
    /// \brief "horizontalAperture"
    /// 
    /// UsdGeomCamera
    const TfToken horizontalAperture;
    /// \brief "horizontalApertureOffset"
    /// 
    /// UsdGeomCamera
    const TfToken horizontalApertureOffset;
    /// \brief "ids"
    /// 
    /// UsdGeomPointInstancer, UsdGeomPoints
    const TfToken ids;
    /// \brief "inactiveIds"
    /// 
    /// int64listop prim metadata that specifies the PointInstancer ids that should be masked (unrenderable) over all time.
    const TfToken inactiveIds;
    /// \brief "indices"
    /// 
    /// UsdGeomSubset
    const TfToken indices;
    /// \brief "inherited"
    /// 
    /// Possible value for UsdGeomImageable::GetVisibilityAttr(), Default value for UsdGeomImageable::GetVisibilityAttr()
    const TfToken inherited;
    /// \brief "interpolateBoundary"
    /// 
    /// UsdGeomMesh
    const TfToken interpolateBoundary;
    /// \brief "interpolation"
    /// 
    /// UsdGeomPrimvar - How a Primvar interpolates across a primitive; equivalent to RenderMan's \ref Usd_InterpolationVals "class specifier" 
    const TfToken interpolation;
    /// \brief "invisible"
    /// 
    /// Possible value for UsdGeomImageable::GetVisibilityAttr()
    const TfToken invisible;
    /// \brief "invisibleIds"
    /// 
    /// UsdGeomPointInstancer
    const TfToken invisibleIds;
    /// \brief "knots"
    /// 
    /// UsdGeomNurbsCurves
    const TfToken knots;
    /// \brief "left"
    /// 
    /// Possible value for UsdGeomCamera::GetStereoRoleAttr()
    const TfToken left;
    /// \brief "leftHanded"
    /// 
    /// Possible value for UsdGeomGprim::GetOrientationAttr()
    const TfToken leftHanded;
    /// \brief "length"
    /// 
    /// UsdGeomPlane
    const TfToken length;
    /// \brief "linear"
    /// 
    /// Possible value for UsdGeomBasisCurves::GetTypeAttr()
    const TfToken linear;
    /// \brief "loop"
    /// 
    /// Possible value for UsdGeomMesh::GetSubdivisionSchemeAttr()
    const TfToken loop;
    /// \brief "metersPerUnit"
    /// 
    /// Stage-level metadata that encodes a scene's linear unit of measure as meters per encoded unit.
    const TfToken metersPerUnit;
    /// \brief "model:applyDrawMode"
    /// 
    /// UsdGeomModelAPI
    const TfToken modelApplyDrawMode;
    /// \brief "model:cardGeometry"
    /// 
    /// UsdGeomModelAPI
    const TfToken modelCardGeometry;
    /// \brief "model:cardTextureXNeg"
    /// 
    /// UsdGeomModelAPI
    const TfToken modelCardTextureXNeg;
    /// \brief "model:cardTextureXPos"
    /// 
    /// UsdGeomModelAPI
    const TfToken modelCardTextureXPos;
    /// \brief "model:cardTextureYNeg"
    /// 
    /// UsdGeomModelAPI
    const TfToken modelCardTextureYNeg;
    /// \brief "model:cardTextureYPos"
    /// 
    /// UsdGeomModelAPI
    const TfToken modelCardTextureYPos;
    /// \brief "model:cardTextureZNeg"
    /// 
    /// UsdGeomModelAPI
    const TfToken modelCardTextureZNeg;
    /// \brief "model:cardTextureZPos"
    /// 
    /// UsdGeomModelAPI
    const TfToken modelCardTextureZPos;
    /// \brief "model:drawMode"
    /// 
    /// UsdGeomModelAPI
    const TfToken modelDrawMode;
    /// \brief "model:drawModeColor"
    /// 
    /// UsdGeomModelAPI
    const TfToken modelDrawModeColor;
    /// \brief "mono"
    /// 
    /// Possible value for UsdGeomCamera::GetStereoRoleAttr(), Default value for UsdGeomCamera::GetStereoRoleAttr()
    const TfToken mono;
    /// \brief "motion:velocityScale"
    /// 
    /// UsdGeomMotionAPI
    const TfToken motionVelocityScale;
    /// \brief "none"
    /// 
    /// Possible value for UsdGeomMesh::GetInterpolateBoundaryAttr(), Possible value for UsdGeomMesh::GetFaceVaryingLinearInterpolationAttr(), Possible value for UsdGeomMesh::GetSubdivisionSchemeAttr()
    const TfToken none;
    /// \brief "nonOverlapping"
    /// 
    /// A type of family of GeomSubsets. It implies that  the elements in the various subsets belonging to the family are  mutually exclusive, i.e., an element that appears in one  subset may not belong to any other subset in the family.
    const TfToken nonOverlapping;
    /// \brief "nonperiodic"
    /// 
    /// Possible value for UsdGeomBasisCurves::GetWrapAttr(), Default value for UsdGeomBasisCurves::GetWrapAttr()
    const TfToken nonperiodic;
    /// \brief "normals"
    /// 
    /// UsdGeomPointBased
    const TfToken normals;
    /// \brief "open"
    /// 
    /// Possible value for UsdGeomNurbsPatch::GetVFormAttr(), Default value for UsdGeomNurbsPatch::GetVFormAttr(), Possible value for UsdGeomNurbsPatch::GetUFormAttr(), Default value for UsdGeomNurbsPatch::GetUFormAttr()
    const TfToken open;
    /// \brief "order"
    /// 
    /// UsdGeomNurbsCurves
    const TfToken order;
    /// \brief "orientation"
    /// 
    /// UsdGeomGprim
    const TfToken orientation;
    /// \brief "orientations"
    /// 
    /// UsdGeomPointInstancer
    const TfToken orientations;
    /// \brief "origin"
    /// 
    /// Possible value for UsdGeomModelAPI::GetModelDrawModeAttr()
    const TfToken origin;
    /// \brief "orthographic"
    /// 
    /// Possible value for UsdGeomCamera::GetProjectionAttr()
    const TfToken orthographic;
    /// \brief "partition"
    /// 
    /// A type of family of GeomSubsets. It implies  that every element appears exacly once in only one of the  subsets in the family.
    const TfToken partition;
    /// \brief "periodic"
    /// 
    /// Possible value for UsdGeomBasisCurves::GetWrapAttr(), Possible value for UsdGeomNurbsPatch::GetVFormAttr(), Possible value for UsdGeomNurbsPatch::GetUFormAttr()
    const TfToken periodic;
    /// \brief "perspective"
    /// 
    /// Possible value for UsdGeomCamera::GetProjectionAttr(), Default value for UsdGeomCamera::GetProjectionAttr()
    const TfToken perspective;
    /// \brief "pinned"
    /// 
    /// Possible value for UsdGeomBasisCurves::GetWrapAttr()
    const TfToken pinned;
    /// \brief "points"
    /// 
    /// UsdGeomPointBased
    const TfToken points;
    /// \brief "pointWeights"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken pointWeights;
    /// \brief "positions"
    /// 
    /// UsdGeomPointInstancer
    const TfToken positions;
    /// \brief "power"
    /// 
    /// Possible value for UsdGeomBasisCurves::GetBasisAttr()
    const TfToken power;
    /// \brief "primvars:displayColor"
    /// 
    /// UsdGeomGprim
    const TfToken primvarsDisplayColor;
    /// \brief "primvars:displayOpacity"
    /// 
    /// UsdGeomGprim
    const TfToken primvarsDisplayOpacity;
    /// \brief "primvars:st"
    /// 
    /// UsdGeomPlane
    const TfToken primvarsSt;
    /// \brief "projection"
    /// 
    /// UsdGeomCamera
    const TfToken projection;
    /// \brief "protoIndices"
    /// 
    /// UsdGeomPointInstancer
    const TfToken protoIndices;
    /// \brief "prototypes"
    /// 
    /// UsdGeomPointInstancer
    const TfToken prototypes;
    /// \brief "proxy"
    /// 
    /// Possible value for UsdGeomImageable::GetPurposeAttr()
    const TfToken proxy;
    /// \brief "proxyPrim"
    /// 
    /// UsdGeomImageable
    const TfToken proxyPrim;
    /// \brief "purpose"
    /// 
    /// UsdGeomImageable
    const TfToken purpose;
    /// \brief "radius"
    /// 
    /// UsdGeomCone, UsdGeomCapsule, UsdGeomCylinder, UsdGeomSphere
    const TfToken radius;
    /// \brief "ranges"
    /// 
    /// UsdGeomNurbsCurves
    const TfToken ranges;
    /// \brief "render"
    /// 
    /// Possible value for UsdGeomImageable::GetPurposeAttr()
    const TfToken render;
    /// \brief "right"
    /// 
    /// Possible value for UsdGeomCamera::GetStereoRoleAttr()
    const TfToken right;
    /// \brief "rightHanded"
    /// 
    /// Possible value for UsdGeomGprim::GetOrientationAttr(), Default value for UsdGeomGprim::GetOrientationAttr()
    const TfToken rightHanded;
    /// \brief "scales"
    /// 
    /// UsdGeomPointInstancer
    const TfToken scales;
    /// \brief "shutter:close"
    /// 
    /// UsdGeomCamera
    const TfToken shutterClose;
    /// \brief "shutter:open"
    /// 
    /// UsdGeomCamera
    const TfToken shutterOpen;
    /// \brief "size"
    /// 
    /// UsdGeomCube
    const TfToken size;
    /// \brief "smooth"
    /// 
    /// Possible value for UsdGeomMesh::GetTriangleSubdivisionRuleAttr()
    const TfToken smooth;
    /// \brief "stereoRole"
    /// 
    /// UsdGeomCamera
    const TfToken stereoRole;
    /// \brief "subdivisionScheme"
    /// 
    /// UsdGeomMesh
    const TfToken subdivisionScheme;
    /// \brief "triangleSubdivisionRule"
    /// 
    /// UsdGeomMesh
    const TfToken triangleSubdivisionRule;
    /// \brief "trimCurve:counts"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken trimCurveCounts;
    /// \brief "trimCurve:knots"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken trimCurveKnots;
    /// \brief "trimCurve:orders"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken trimCurveOrders;
    /// \brief "trimCurve:points"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken trimCurvePoints;
    /// \brief "trimCurve:ranges"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken trimCurveRanges;
    /// \brief "trimCurve:vertexCounts"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken trimCurveVertexCounts;
    /// \brief "type"
    /// 
    /// UsdGeomBasisCurves
    const TfToken type;
    /// \brief "uForm"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken uForm;
    /// \brief "uKnots"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken uKnots;
    /// \brief "unauthoredValuesIndex"
    /// 
    /// UsdGeomPrimvar - The index that represents  unauthored values in the indices array of an indexed primvar.
    const TfToken unauthoredValuesIndex;
    /// \brief "uniform"
    /// 
    /// Possible value for UsdGeomPrimvar::SetInterpolation. One value remains constant for each uv patch segment of the surface primitive (which is a \em face for meshes).
    const TfToken uniform;
    /// \brief "unrestricted"
    /// 
    /// A type of family of GeomSubsets. It implies that there are no restrictions w.r.t. the membership of elements in  the subsets. There could be overlapping members in subsets  belonging to the family and the union of all subsets in the  family may not contain all the elements.
    const TfToken unrestricted;
    /// \brief "uOrder"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken uOrder;
    /// \brief "upAxis"
    /// 
    /// Stage-level metadata that encodes a scene's orientation as a token whose value can be "Y" or "Z".
    const TfToken upAxis;
    /// \brief "uRange"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken uRange;
    /// \brief "uVertexCount"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken uVertexCount;
    /// \brief "varying"
    /// 
    /// Possible value for UsdGeomPrimvar::SetInterpolation. Four values are interpolated over each uv patch segment of the  surface. Bilinear interpolation is used for interpolation  between the four values.
    const TfToken varying;
    /// \brief "velocities"
    /// 
    /// UsdGeomPointInstancer, UsdGeomPointBased
    const TfToken velocities;
    /// \brief "vertex"
    /// 
    /// Possible value for UsdGeomPrimvar::SetInterpolation. Values are interpolated between each vertex in the surface primitive. The basis function of the surface is used for  interpolation between vertices.
    const TfToken vertex;
    /// \brief "verticalAperture"
    /// 
    /// UsdGeomCamera
    const TfToken verticalAperture;
    /// \brief "verticalApertureOffset"
    /// 
    /// UsdGeomCamera
    const TfToken verticalApertureOffset;
    /// \brief "vForm"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken vForm;
    /// \brief "visibility"
    /// 
    /// UsdGeomImageable
    const TfToken visibility;
    /// \brief "vKnots"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken vKnots;
    /// \brief "vOrder"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken vOrder;
    /// \brief "vRange"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken vRange;
    /// \brief "vVertexCount"
    /// 
    /// UsdGeomNurbsPatch
    const TfToken vVertexCount;
    /// \brief "width"
    /// 
    /// UsdGeomPlane
    const TfToken width;
    /// \brief "widths"
    /// 
    /// UsdGeomPoints, UsdGeomCurves
    const TfToken widths;
    /// \brief "wrap"
    /// 
    /// UsdGeomBasisCurves
    const TfToken wrap;
    /// \brief "X"
    /// 
    /// Possible value for UsdGeomPlane::GetAxisAttr(), Possible value for UsdGeomCone::GetAxisAttr(), Possible value for UsdGeomCapsule::GetAxisAttr(), Possible value for UsdGeomCylinder::GetAxisAttr()
    const TfToken x;
    /// \brief "xformOpOrder"
    /// 
    /// UsdGeomXformable
    const TfToken xformOpOrder;
    /// \brief "Y"
    /// 
    /// Possible value for UsdGeomPlane::GetAxisAttr(), Possible value for UsdGeomCone::GetAxisAttr(), Possible value for UsdGeomCapsule::GetAxisAttr(), Possible value for UsdGeomCylinder::GetAxisAttr()
    const TfToken y;
    /// \brief "Z"
    /// 
    /// Possible value for UsdGeomPlane::GetAxisAttr(), Default value for UsdGeomPlane::GetAxisAttr(), Possible value for UsdGeomCone::GetAxisAttr(), Default value for UsdGeomCone::GetAxisAttr(), Possible value for UsdGeomCapsule::GetAxisAttr(), Default value for UsdGeomCapsule::GetAxisAttr(), Possible value for UsdGeomCylinder::GetAxisAttr(), Default value for UsdGeomCylinder::GetAxisAttr()
    const TfToken z;
    /// A vector of all of the tokens listed above.
    const std::vector<TfToken> allTokens;
};

/// \var UsdGeomTokens
///
/// A global variable with static, efficient \link TfToken TfTokens\endlink
/// for use in all public USD API.  \sa UsdGeomTokensType
extern USDGEOM_API TfStaticData<UsdGeomTokensType> UsdGeomTokens;

PXR_NAMESPACE_CLOSE_SCOPE

#endif
