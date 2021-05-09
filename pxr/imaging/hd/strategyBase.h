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
#ifndef PXR_IMAGING_HD_STRATEGY_BASE_H
#define PXR_IMAGING_HD_STRATEGY_BASE_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/api.h"
#include "pxr/imaging/hd/version.h"
#include "pxr/imaging/hd/bufferSpec.h"
#include "pxr/imaging/hd/bufferArray.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/vt/dictionary.h"

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE


using HdBufferArraySharedPtr = std::shared_ptr<class HdBufferArray>;
using HdBufferArrayRangeSharedPtr = std::shared_ptr<class HdBufferArrayRange>;

/// \class HdAggregationStrategy
///
/// Aggregation strategy base class.
///
class HdAggregationStrategy {
public:
    /// Aggregation ID
    typedef size_t AggregationId;

    HD_API
    virtual ~HdAggregationStrategy();

    /// Factory for creating HdBufferArray
    virtual HdBufferArraySharedPtr CreateBufferArray(
        TfToken const &role,
        HdBufferSpecVector const &bufferSpecs,
        HdBufferArrayUsageHint usageHint) = 0;

    /// Factory for creating HdBufferArrayRange
    virtual HdBufferArrayRangeSharedPtr CreateBufferArrayRange() = 0;
        

    /// Returns id for given bufferSpecs to be used for aggregation
    virtual AggregationId ComputeAggregationId(
        HdBufferSpecVector const &bufferSpecs,
        HdBufferArrayUsageHint usageHint) const = 0;

    /// Returns the buffer specs from a given buffer array
    virtual HdBufferSpecVector GetBufferSpecs(
        HdBufferArraySharedPtr const &bufferArray) const = 0;

    /// Returns the accumulated GPU resource allocation
    /// for items in the BufferArray passed as parameter
    virtual size_t GetResourceAllocation(
        HdBufferArraySharedPtr const &bufferArray, 
        VtDictionary &result) const = 0;

    /// (Optional) called to Flush consolidated / staging buffers.
    HD_API
    virtual void Flush() {}
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif  // PXR_IMAGING_HD_STRATEGY_BASE_H
