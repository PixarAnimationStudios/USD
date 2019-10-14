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
#include "pxr/pxr.h"
#include "pxr/base/tf/pyModule.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_WRAP_MODULE
{
    TF_WRAP(UsdGeomBBoxCache); 
    TF_WRAP(UsdGeomConstraintTarget);
    TF_WRAP(UsdGeomModelAPI);
    TF_WRAP(UsdGeomPrimvar);
    TF_WRAP(UsdGeomPrimvarsAPI);
    TF_WRAP(UsdGeomTokens);
    TF_WRAP(UsdGeomXformOp);
    TF_WRAP(UsdGeomXformCommonAPI);
    TF_WRAP(UsdGeomXformCache);
    TF_WRAP(Metrics);
    TF_WRAP(UsdGeomMotionAPI);
    
    // Generated schema.  Base classes must precede derived classes.
    // Indentation shows class hierarchy.
    TF_WRAP(UsdGeomImageable);
        TF_WRAP(UsdGeomScope);
        TF_WRAP(UsdGeomXformable);
            TF_WRAP(UsdGeomXform);
            TF_WRAP(UsdGeomCamera);
            TF_WRAP(UsdGeomBoundable);
                TF_WRAP(UsdGeomGprim);
                    TF_WRAP(UsdGeomCapsule);
                    TF_WRAP(UsdGeomCone);
                    TF_WRAP(UsdGeomCube);
                    TF_WRAP(UsdGeomCylinder);
                    TF_WRAP(UsdGeomSphere);
                    TF_WRAP(UsdGeomPlane);
                    TF_WRAP(UsdGeomPointBased);
                        TF_WRAP(UsdGeomMesh);
                        TF_WRAP(UsdGeomNurbsPatch);
                        TF_WRAP(UsdGeomPoints);
                        TF_WRAP(UsdGeomCurves);
                            TF_WRAP(UsdGeomBasisCurves);
                            TF_WRAP(UsdGeomNurbsCurves);
                TF_WRAP(UsdGeomPointInstancer);
    TF_WRAP(UsdGeomSubset);
}
