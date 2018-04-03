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
#include "pxr/usd/usdGeom/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

UsdGeomTokensType::UsdGeomTokensType() :
    all("all", TfToken::Immortal),
    angularVelocities("angularVelocities", TfToken::Immortal),
    axis("axis", TfToken::Immortal),
    basis("basis", TfToken::Immortal),
    bezier("bezier", TfToken::Immortal),
    bilinear("bilinear", TfToken::Immortal),
    boundaries("boundaries", TfToken::Immortal),
    bounds("bounds", TfToken::Immortal),
    box("box", TfToken::Immortal),
    bspline("bspline", TfToken::Immortal),
    cards("cards", TfToken::Immortal),
    catmullClark("catmullClark", TfToken::Immortal),
    catmullRom("catmullRom", TfToken::Immortal),
    clippingPlanes("clippingPlanes", TfToken::Immortal),
    clippingRange("clippingRange", TfToken::Immortal),
    closed("closed", TfToken::Immortal),
    collection("collection", TfToken::Immortal),
    constant("constant", TfToken::Immortal),
    cornerIndices("cornerIndices", TfToken::Immortal),
    cornerSharpnesses("cornerSharpnesses", TfToken::Immortal),
    cornersOnly("cornersOnly", TfToken::Immortal),
    cornersPlus1("cornersPlus1", TfToken::Immortal),
    cornersPlus2("cornersPlus2", TfToken::Immortal),
    creaseIndices("creaseIndices", TfToken::Immortal),
    creaseLengths("creaseLengths", TfToken::Immortal),
    creaseSharpnesses("creaseSharpnesses", TfToken::Immortal),
    cross("cross", TfToken::Immortal),
    cubic("cubic", TfToken::Immortal),
    curveVertexCounts("curveVertexCounts", TfToken::Immortal),
    default_("default", TfToken::Immortal),
    doubleSided("doubleSided", TfToken::Immortal),
    edgeAndCorner("edgeAndCorner", TfToken::Immortal),
    edgeOnly("edgeOnly", TfToken::Immortal),
    elementSize("elementSize", TfToken::Immortal),
    elementType("elementType", TfToken::Immortal),
    extent("extent", TfToken::Immortal),
    extentsHint("extentsHint", TfToken::Immortal),
    face("face", TfToken::Immortal),
    faceSet("faceSet", TfToken::Immortal),
    faceVarying("faceVarying", TfToken::Immortal),
    faceVaryingLinearInterpolation("faceVaryingLinearInterpolation", TfToken::Immortal),
    faceVertexCounts("faceVertexCounts", TfToken::Immortal),
    faceVertexIndices("faceVertexIndices", TfToken::Immortal),
    familyName("familyName", TfToken::Immortal),
    focalLength("focalLength", TfToken::Immortal),
    focusDistance("focusDistance", TfToken::Immortal),
    form("form", TfToken::Immortal),
    fromTexture("fromTexture", TfToken::Immortal),
    fStop("fStop", TfToken::Immortal),
    guide("guide", TfToken::Immortal),
    height("height", TfToken::Immortal),
    hermite("hermite", TfToken::Immortal),
    holeIndices("holeIndices", TfToken::Immortal),
    horizontalAperture("horizontalAperture", TfToken::Immortal),
    horizontalApertureOffset("horizontalApertureOffset", TfToken::Immortal),
    ids("ids", TfToken::Immortal),
    inactiveIds("inactiveIds", TfToken::Immortal),
    indices("indices", TfToken::Immortal),
    inherited("inherited", TfToken::Immortal),
    interpolateBoundary("interpolateBoundary", TfToken::Immortal),
    interpolation("interpolation", TfToken::Immortal),
    invisible("invisible", TfToken::Immortal),
    invisibleIds("invisibleIds", TfToken::Immortal),
    knots("knots", TfToken::Immortal),
    left("left", TfToken::Immortal),
    leftHanded("leftHanded", TfToken::Immortal),
    linear("linear", TfToken::Immortal),
    loop("loop", TfToken::Immortal),
    modelApplyDrawMode("model:applyDrawMode", TfToken::Immortal),
    modelCardGeometry("model:cardGeometry", TfToken::Immortal),
    modelCardTextureXNeg("model:cardTextureXNeg", TfToken::Immortal),
    modelCardTextureXPos("model:cardTextureXPos", TfToken::Immortal),
    modelCardTextureYNeg("model:cardTextureYNeg", TfToken::Immortal),
    modelCardTextureYPos("model:cardTextureYPos", TfToken::Immortal),
    modelCardTextureZNeg("model:cardTextureZNeg", TfToken::Immortal),
    modelCardTextureZPos("model:cardTextureZPos", TfToken::Immortal),
    modelDrawMode("model:drawMode", TfToken::Immortal),
    modelDrawModeColor("model:drawModeColor", TfToken::Immortal),
    mono("mono", TfToken::Immortal),
    motionVelocityScale("motion:velocityScale", TfToken::Immortal),
    none("none", TfToken::Immortal),
    nonOverlapping("nonOverlapping", TfToken::Immortal),
    nonperiodic("nonperiodic", TfToken::Immortal),
    normals("normals", TfToken::Immortal),
    open("open", TfToken::Immortal),
    order("order", TfToken::Immortal),
    orientation("orientation", TfToken::Immortal),
    orientations("orientations", TfToken::Immortal),
    origin("origin", TfToken::Immortal),
    orthographic("orthographic", TfToken::Immortal),
    partition("partition", TfToken::Immortal),
    periodic("periodic", TfToken::Immortal),
    perspective("perspective", TfToken::Immortal),
    points("points", TfToken::Immortal),
    pointWeights("pointWeights", TfToken::Immortal),
    positions("positions", TfToken::Immortal),
    power("power", TfToken::Immortal),
    primvarsDisplayColor("primvars:displayColor", TfToken::Immortal),
    primvarsDisplayOpacity("primvars:displayOpacity", TfToken::Immortal),
    projection("projection", TfToken::Immortal),
    protoIndices("protoIndices", TfToken::Immortal),
    prototypes("prototypes", TfToken::Immortal),
    proxy("proxy", TfToken::Immortal),
    proxyPrim("proxyPrim", TfToken::Immortal),
    purpose("purpose", TfToken::Immortal),
    radius("radius", TfToken::Immortal),
    ranges("ranges", TfToken::Immortal),
    render("render", TfToken::Immortal),
    right("right", TfToken::Immortal),
    rightHanded("rightHanded", TfToken::Immortal),
    scales("scales", TfToken::Immortal),
    shutterClose("shutter:close", TfToken::Immortal),
    shutterOpen("shutter:open", TfToken::Immortal),
    size("size", TfToken::Immortal),
    smooth("smooth", TfToken::Immortal),
    stereoRole("stereoRole", TfToken::Immortal),
    subdivisionScheme("subdivisionScheme", TfToken::Immortal),
    triangleSubdivisionRule("triangleSubdivisionRule", TfToken::Immortal),
    trimCurveCounts("trimCurve:counts", TfToken::Immortal),
    trimCurveKnots("trimCurve:knots", TfToken::Immortal),
    trimCurveOrders("trimCurve:orders", TfToken::Immortal),
    trimCurvePoints("trimCurve:points", TfToken::Immortal),
    trimCurveRanges("trimCurve:ranges", TfToken::Immortal),
    trimCurveVertexCounts("trimCurve:vertexCounts", TfToken::Immortal),
    type("type", TfToken::Immortal),
    uForm("uForm", TfToken::Immortal),
    uKnots("uKnots", TfToken::Immortal),
    unauthoredValuesIndex("unauthoredValuesIndex", TfToken::Immortal),
    uniform("uniform", TfToken::Immortal),
    unrestricted("unrestricted", TfToken::Immortal),
    uOrder("uOrder", TfToken::Immortal),
    upAxis("upAxis", TfToken::Immortal),
    uRange("uRange", TfToken::Immortal),
    uVertexCount("uVertexCount", TfToken::Immortal),
    varying("varying", TfToken::Immortal),
    velocities("velocities", TfToken::Immortal),
    vertex("vertex", TfToken::Immortal),
    verticalAperture("verticalAperture", TfToken::Immortal),
    verticalApertureOffset("verticalApertureOffset", TfToken::Immortal),
    vForm("vForm", TfToken::Immortal),
    visibility("visibility", TfToken::Immortal),
    vKnots("vKnots", TfToken::Immortal),
    vOrder("vOrder", TfToken::Immortal),
    vRange("vRange", TfToken::Immortal),
    vVertexCount("vVertexCount", TfToken::Immortal),
    widths("widths", TfToken::Immortal),
    wrap("wrap", TfToken::Immortal),
    x("X", TfToken::Immortal),
    xformOpOrder("xformOpOrder", TfToken::Immortal),
    y("Y", TfToken::Immortal),
    z("Z", TfToken::Immortal),
    allTokens({
        all,
        angularVelocities,
        axis,
        basis,
        bezier,
        bilinear,
        boundaries,
        bounds,
        box,
        bspline,
        cards,
        catmullClark,
        catmullRom,
        clippingPlanes,
        clippingRange,
        closed,
        collection,
        constant,
        cornerIndices,
        cornerSharpnesses,
        cornersOnly,
        cornersPlus1,
        cornersPlus2,
        creaseIndices,
        creaseLengths,
        creaseSharpnesses,
        cross,
        cubic,
        curveVertexCounts,
        default_,
        doubleSided,
        edgeAndCorner,
        edgeOnly,
        elementSize,
        elementType,
        extent,
        extentsHint,
        face,
        faceSet,
        faceVarying,
        faceVaryingLinearInterpolation,
        faceVertexCounts,
        faceVertexIndices,
        familyName,
        focalLength,
        focusDistance,
        form,
        fromTexture,
        fStop,
        guide,
        height,
        hermite,
        holeIndices,
        horizontalAperture,
        horizontalApertureOffset,
        ids,
        inactiveIds,
        indices,
        inherited,
        interpolateBoundary,
        interpolation,
        invisible,
        invisibleIds,
        knots,
        left,
        leftHanded,
        linear,
        loop,
        modelApplyDrawMode,
        modelCardGeometry,
        modelCardTextureXNeg,
        modelCardTextureXPos,
        modelCardTextureYNeg,
        modelCardTextureYPos,
        modelCardTextureZNeg,
        modelCardTextureZPos,
        modelDrawMode,
        modelDrawModeColor,
        mono,
        motionVelocityScale,
        none,
        nonOverlapping,
        nonperiodic,
        normals,
        open,
        order,
        orientation,
        orientations,
        origin,
        orthographic,
        partition,
        periodic,
        perspective,
        points,
        pointWeights,
        positions,
        power,
        primvarsDisplayColor,
        primvarsDisplayOpacity,
        projection,
        protoIndices,
        prototypes,
        proxy,
        proxyPrim,
        purpose,
        radius,
        ranges,
        render,
        right,
        rightHanded,
        scales,
        shutterClose,
        shutterOpen,
        size,
        smooth,
        stereoRole,
        subdivisionScheme,
        triangleSubdivisionRule,
        trimCurveCounts,
        trimCurveKnots,
        trimCurveOrders,
        trimCurvePoints,
        trimCurveRanges,
        trimCurveVertexCounts,
        type,
        uForm,
        uKnots,
        unauthoredValuesIndex,
        uniform,
        unrestricted,
        uOrder,
        upAxis,
        uRange,
        uVertexCount,
        varying,
        velocities,
        vertex,
        verticalAperture,
        verticalApertureOffset,
        vForm,
        visibility,
        vKnots,
        vOrder,
        vRange,
        vVertexCount,
        widths,
        wrap,
        x,
        xformOpOrder,
        y,
        z
    })
{
}

TfStaticData<UsdGeomTokensType> UsdGeomTokens;

PXR_NAMESPACE_CLOSE_SCOPE
