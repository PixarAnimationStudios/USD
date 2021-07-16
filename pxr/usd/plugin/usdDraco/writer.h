//
// Copyright 2019 Google LLC
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

#ifndef PXR_USD_PLUGIN_USD_DRACO_WRITER_H
#define PXR_USD_PLUGIN_USD_DRACO_WRITER_H

#include "pxr/pxr.h"
#include "pxr/usd/plugin/usdDraco/api.h"
#include "pxr/usd/usdGeom/mesh.h"

#include <string>


PXR_NAMESPACE_OPEN_SCOPE


/// Encodes mesh and writes it in Draco format to a file at \p fileName.
USDDRACO_API
bool UsdDraco_WriteDraco(const UsdGeomMesh &mesh,
                         const std::string &fileName,
                         int qp,
                         int qt,
                         int qn,
                         int cl,
                         int preservePolygons,
                         int preservePositionOrder,
                         int preserveHoles);

/// Checks whether a USD primvar can be encoded to Draco. It is called from
/// usdcompress.py script to determine whether a primvar should be deleted from
/// USD mesh or remain in USD mesh.
USDDRACO_API
bool UsdDraco_PrimvarSupported(const UsdGeomPrimvar &primvar);


PXR_NAMESPACE_CLOSE_SCOPE

#endif  // PXR_USD_PLUGIN_USD_DRACO_WRITER_H
