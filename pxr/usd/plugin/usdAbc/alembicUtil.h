//
// Copyright 2016-2019 Pixar
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
#ifndef PXR_USD_PLUGIN_USD_ABC_ALEMBIC_UTIL_H
#define PXR_USD_PLUGIN_USD_ABC_ALEMBIC_UTIL_H

/// \file usdAbc/alembicUtil.h

#include "pxr/pxr.h"
#include "pxr/usd/plugin/usdAbc/alembicReader.h"
#include "pxr/usd/sdf/abstractData.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/value.h"
#include "pxr/base/arch/demangle.h"
#include "pxr/base/tf/staticTokens.h"
#include <Alembic/Abc/ICompoundProperty.h>
#include <Alembic/Abc/ISampleSelector.h>
#include <boost/call_traits.hpp>
#include <boost/operators.hpp>
#include <boost/optional.hpp>
#include <boost/shared_array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/type_traits/remove_const.hpp>
#include <boost/type_traits/remove_reference.hpp>
#include <boost/variant.hpp>

#include <functional>
#include <algorithm>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>


namespace Alembic {
namespace Util {
namespace ALEMBIC_VERSION_NS {
    template <> struct PODTraitsFromType<PXR_NS::GfHalf> 
        : public Float16PODTraits {};
}}}// end namespace Alembic


PXR_NAMESPACE_OPEN_SCOPE


class SdfAbstractDataValue;

/// Flags for readers and writers.
#define USDABC_ALEMBIC_CONTEXT_FLAG_NAMES \
    (verbose) \
    (expandInstances) \
    (disableInstancing) \
    (promoteInstances) \
    /* end */
TF_DECLARE_PUBLIC_TOKENS(UsdAbc_AlembicContextFlagNames,
                         USDABC_ALEMBIC_CONTEXT_FLAG_NAMES);

// A namespace so we can bring Alembic namespaces into it.
namespace UsdAbc_AlembicUtil {

using namespace ::Alembic::Abc;

// Prim type names in the UsdGeom schema except we create new names for
// types that don't map directly to Alembic.
#define USD_ABC_PRIM_TYPE_NAMES \
    (BasisCurves) \
    (Camera) \
    (HermiteCurves) \
    (Mesh) \
    (NurbsCurves) \
    (Points) \
    (PolyMesh) \
    (PseudoRoot) \
    (Scope) \
    (Xform) \
    (GeomSubset)\
    /* end */
TF_DECLARE_PUBLIC_TOKENS(UsdAbcPrimTypeNames, USD_ABC_PRIM_TYPE_NAMES);

// Property names in the UsdGeom schema.
#define USD_ABC_GPRIM_NAMES \
    (primvars) \
    (userProperties) \
    ((defaultFamilyName, "materialBind")) \
    ((defaultFamilyTypeAttributeName, "subsetFamily:materialBind:familyType")) \
    /* end */
#define USD_ABC_POINTBASED_NAMES \
    ((uv, "primvars:uv")) \
    ((uvIndices, "primvars:uv:indices")) \
    ((st, "primvars:st")) \
    ((stIndices, "primvars:st:indices")) \
    /* end */
#define USD_ABC_PROPERTY_NAMES \
    USD_ABC_GPRIM_NAMES \
    USD_ABC_POINTBASED_NAMES \
    (locator) \
    /* end */
TF_DECLARE_PUBLIC_TOKENS(UsdAbcPropertyNames, USD_ABC_PROPERTY_NAMES);

#define USD_ABC_CUSTOM_METADATA \
    (gprimDataRender) \
    (riName) \
    (riType) \
    (singleSampleAsDefault) \
    /* end */
TF_DECLARE_PUBLIC_TOKENS(UsdAbcCustomMetadata, USD_ABC_CUSTOM_METADATA);

//
// Alembic property value types.
//

/// A type to represent an Alembic value type.  An Alembic DataType has
/// a POD and extent but not scalar vs array;  this type includes that
/// extra bit.  It also supports compound types as their schema titles.
struct UsdAbc_AlembicType : boost::totally_ordered<UsdAbc_AlembicType> {
    PlainOldDataType pod;     // POD type in scalar and array.
    uint8_t extent;           // Extent of POD (e.g. 3 for a 3-tuple).
    bool_t array;             // true for array, false otherwise.

    // An empty type.
    UsdAbc_AlembicType() :
        pod(kUnknownPOD), extent(0), array(false)
    {
        // Do nothing
    }

    // An array or scalar type.
    UsdAbc_AlembicType(PlainOldDataType pod_, uint8_t extent_, bool_t array_) :
        pod(pod_), extent(extent_), array(array_)
    {
        // Do nothing
    }

    // An Alembic property's type.
    UsdAbc_AlembicType(const PropertyHeader& header) :
        pod(header.getPropertyType() == kCompoundProperty ?
                kUnknownPOD : header.getDataType().getPod()),
        extent(header.getPropertyType() == kCompoundProperty ?
                0 : header.getDataType().getExtent()),
        array(header.getPropertyType() == kArrayProperty)
    {
        // Do nothing
    }

    bool IsEmpty() const
    {
        return pod == kUnknownPOD;
    }

    /// Returns the corresponding Alembic DataType.  This returns the
    /// unknown type for compound types.
    DataType GetDataType() const
    {
        return DataType(pod, extent);
    }

    PropertyType GetPropertyType() const
    {
        return array ? kArrayProperty : kScalarProperty;
    }

    // Debugging
    std::string Stringify() const;

    bool operator==(const UsdAbc_AlembicType& rhs) const;
    bool operator<(const UsdAbc_AlembicType& rhs) const;
};

//
// Property value wrappers.
//

/// Wraps a VtValue or SdfAbstractDataValue so we can access any
/// the same way.  This type allows us to implement some methods without
/// templatizing them.
class UsdAbc_AlembicDataAny {
public:
    /// Construct an empty any.
    UsdAbc_AlembicDataAny() { }

    /// Construct with a pointer to any supported type-erased object \p any.
    /// If \p any is \c NULL then this object is considered to be empty.
    template <class T>
    explicit UsdAbc_AlembicDataAny(T* any)
    {
        if (any) {
            _valuePtr = any;
        }
    }

    /// Assigns \p rhs to the value passed in the c'tor.
    bool Set(const VtValue& rhs) const
    {
        return boost::apply_visitor(_Set(rhs), _valuePtr);
    }

    /// Assigns \p rhs to the value passed in the c'tor.
    template <class T>
    bool Set(T rhs) const
    {
        typedef typename boost::remove_reference<
                    typename boost::remove_const<T>::type>::type Type;
        return boost::apply_visitor(_SetTyped<Type>(rhs), _valuePtr);
    }

    /// Returns \c true iff constructed with a NULL pointer.
    bool IsEmpty() const
    {
        return _valuePtr.which() == 0;
    }

    /// Explicit bool conversion operator. Converts to true iff this object was 
    /// constructed with a non-NULL pointer.
    explicit operator bool() const
    {
        return !IsEmpty();
    }

private:
    // Object representing the NULL pointer.
    class _Empty {};

    // Visitor for assignment.
    struct _Set : public boost::static_visitor<bool> {
        _Set(const VtValue& rhs) : value(rhs) { }

        bool operator()(_Empty) const
        {
            // Convenience for "Has" methods.  Discard the value and
            // return true.
            return true;
        }

        bool operator()(VtValue* dst) const
        {
            *dst = value;
            return true;
        }

        bool operator()(SdfAbstractDataValue* dst) const
        {
            return dst->StoreValue(value);
        }

        const VtValue& value;
    };

    // Visitor for assignment.
    template <class T>
    struct _SetTyped : public boost::static_visitor<bool> {
        _SetTyped(typename boost::call_traits<T>::param_type rhs) : value(rhs){}

        bool operator()(_Empty) const
        {
            // Convenience for "Has" methods.  Discard the value and
            // return true.
            return true;
        }

        bool operator()(VtValue* dst) const
        {
            *dst = value;
            return true;
        }

        bool operator()(SdfAbstractDataValue* dst) const
        {
            return dst->StoreValue(value);
        }

        typename boost::call_traits<T>::param_type value;
    };

private:
    boost::variant<_Empty, VtValue*, SdfAbstractDataValue*> _valuePtr;
};

//
// Usd -> Alembic sample
//

// Helpers for _SampleForAlembic.
template <class T>
struct _ExtractAddressOfSampleForAlembic {
    const void* operator()(const T& value) const
    {
        return &value;
    }
};
template <class T, class A = _ExtractAddressOfSampleForAlembic<T> >
struct _ExtractSampleForAlembic {
    const void* operator()(const VtValue& v, size_t* numSamples) const
    {
        *numSamples = 1;
        return A()(v.UncheckedGet<T>());
    }
};
template <class T>
struct _ExtractSampleForAlembic<VtArray<T> > {
    const void* operator()(const VtValue& v, size_t* numSamples) const
    {
        const VtArray<T>& result = v.UncheckedGet<VtArray<T> >();
        *numSamples = result.size();
        return result.cdata();
    }
};

/// Holds a property value from Usd in a form suitable for passing to Alembic,
/// providing a common interface to several forms of data.
class _SampleForAlembic {
public:
    typedef std::vector<uint32_t> IndexArray;
    typedef boost::shared_ptr<IndexArray> IndexArrayPtr;

    class Error {
    public:
        Error(const std::string& msg) : message(msg) { }
        const std::string& message;
    };

    /// An empty sample.
    _SampleForAlembic() :
        _numSamples(0),
        _value(_HolderValue(new _EmptyHolder))
    {
        // Do nothing
    }

    /// A sample in error.  This doesn't have a value but does report an
    /// error message.
    _SampleForAlembic(const Error& error) :
        _numSamples(0),
        _value(_HolderValue(new _ErrorHolder(error.message)))
    {
        // Do nothing
    }

    /// A sample using a given scalar value of type T.
    template <class T>
    _SampleForAlembic(const T& value) :
        _numSamples(1),
        _value(_HolderValue(new _RawHolder<T>(value)))
    {
        // Do nothing
    }

    /// A sample using a given std::vector of values of type T.
    template <class T>
    _SampleForAlembic(const std::vector<T>& value) :
        _numSamples(value.size()),
        _value(_MakeRawArrayHolder(value))
    {
        // Do nothing
    }

    /// A sample using raw data from a VtValue holding a value extracted
    /// by \p extractor, which must have the function signature:
    /// const void* ()(const VtValue&, size_t* numSamples).
    /// It must return a pointer into the data in the value and set
    /// the numSamples argument to the number of samples in the value.
    template <class E>
    _SampleForAlembic(const VtValue& value, const E& extractor) :
        _value(_HolderValue(new _VtValueHolder(value, &_numSamples, extractor)))
    {
        // Do nothing
    }

    /// A sample using raw data from a shared pointer to a T.
    template <class T>
    _SampleForAlembic(const boost::shared_ptr<T>& value) :
        _numSamples(1),
        _value(_HolderValue(new _ScalarHolder<T>(value)))
    {
        TF_VERIFY(value);
    }

    /// A sample using raw data from a shared pointer to a T[].
    template <class T>
    _SampleForAlembic(const boost::shared_array<T>& values, size_t count) :
        _numSamples(count),
        _value(_HolderValue(new _ArrayHolder<T>(values)))
    {
        TF_VERIFY(values);
    }

    bool IsError(std::string* message) const
    {
        return _value.IsError(message);
    }

    /// Explicit bool conversion operator. Converts to \c true iff the data is 
    /// valid.
    explicit operator bool() const
    {
        return _IsValid();
    }

    /// Returns the raw data.
    const void* GetData() const
    {
        return _value.Get();
    }

    /// Returns the raw data cast to a const T*.  Do not call if this object
    /// evaluates to \c false.
    template <class T>
    const T* GetDataAs() const
    {
        return reinterpret_cast<const T*>(_value.Get());
    }

    /// Returns the number of POD elements.
    size_t GetCount() const
    {
        return _numSamples;
    }

    /// Saves the given indices.
    void SetIndices(const IndexArrayPtr& indices)
    {
        _indices = indices;
    }

    /// Returns the vector of indices last set by \c SetIndices().
    IndexArrayPtr GetIndices() const
    {
        return _indices;
    }

private:
    bool _IsValid() const
    {
        return _value.Get();
    }

    // Type erased holder for pointers.
    class _Holder {
    public:
        virtual ~_Holder();
        virtual const void* Get() const = 0;
        virtual bool Error(std::string*) const;
    };

    // Hold nothing.
    class _EmptyHolder : public _Holder {
    public:
        _EmptyHolder();
        virtual ~_EmptyHolder();
        virtual const void* Get() const { return NULL; }
    };

    // Hold an error and no sample.
    class _ErrorHolder : public _Holder {
    public:
        _ErrorHolder(const std::string& message);
        virtual ~_ErrorHolder();
        virtual const void* Get() const { return NULL; }
        virtual bool Error(std::string*) const;

    private:
        std::string _message;
    };

    // Hold nothing.
    template <class T>
    class _RawHolder : public _Holder {
    public:
        _RawHolder(const T& value) : _value(value) { }
        virtual ~_RawHolder() { }
        virtual const void* Get() const { return &_value; }

    private:
        const T _value;
    };

    // Hold a VtValue.
    class _VtValueHolder : public _Holder {
    public:
        template <class E>
        _VtValueHolder(const VtValue& value,
                       size_t* numSamples, const E& extractor) :
            _value(new VtValue(value)),
            _ptr(extractor(*_value, numSamples)) { }
        virtual ~_VtValueHolder();
        virtual const void* Get() const { return _ptr; }

    private:
        boost::shared_ptr<VtValue> _value;
        const void* _ptr;
    };

    // Hold a shared_ptr.
    template <class T>
    class _ScalarHolder : public _Holder {
    public:
        _ScalarHolder(const boost::shared_ptr<T>& ptr) : _ptr(ptr) { }
        virtual ~_ScalarHolder() { }
        virtual const void* Get() const { return _ptr.get(); }

    private:
        boost::shared_ptr<T> _ptr;
    };

    // Hold a shared_array.
    template <class T>
    class _ArrayHolder : public _Holder {
    public:
        _ArrayHolder(const boost::shared_array<T>& ptr) : _ptr(ptr) { }
        virtual ~_ArrayHolder() { }
        virtual const void* Get() const { return _ptr.get(); }

    private:
        boost::shared_array<T> _ptr;
    };

    // Hold a _Holder as a value type.
    class _HolderValue {
    public:
        explicit _HolderValue(_Holder* holder) : _holder(holder) { }

        const void* Get() const { return _holder->Get(); }
        bool IsError(std::string* msg) const { return _holder->Error(msg); }

    private:
        boost::shared_ptr<_Holder> _holder;
    };

    template <class T>
    static _HolderValue _MakeRawArrayHolder(const std::vector<T>& value)
    {
        boost::shared_array<T> copy(new T[value.size()]);
        std::copy(value.begin(), value.end(), copy.get());
        return _HolderValue(new _ArrayHolder<T>(copy));
    }

private:
    size_t _numSamples;
    _HolderValue _value;
    IndexArrayPtr _indices;
};

_SampleForAlembic _ErrorSampleForAlembic(const std::string& msg);

/// Create an _SampleForAlembic pointing to the raw data in a VtValue.
/// This assumes the VtValue is holding a value of type UsdType.
template <class UsdType>
struct _SampleForAlembicIdentityConverter {
    _SampleForAlembic operator()(const VtValue& value) const
    {
        return _SampleForAlembic(value, _ExtractSampleForAlembic<UsdType>());
    }
};

/// Create an _SampleForAlembic from a VtValue converted by construction
/// to the Alembic type.  This assumes the VtValue is holding a value of
/// type UsdType.
template <class UsdType, class AlembicType>
struct _SampleForAlembicConstructConverter {
    _SampleForAlembic operator()(const VtValue& value) const
    {
        return _SampleForAlembic(boost::shared_ptr<AlembicType>(
            new AlembicType(value.UncheckedGet<UsdType>())));
    }
};
// Special case to identity converter.
template <class U>
struct _SampleForAlembicConstructConverter<U, U> :
    public _SampleForAlembicIdentityConverter<U> {
};

//
// Alembic <-> Usd POD conversions.
//

//
// POD conversion to Usd.
//

template <class UsdType, class AlembicType, size_t extent>
struct _ConvertPODToUsd { };

// No conversion necessary.
template <class UsdType>
struct _ConvertPODToUsd<UsdType, UsdType, 1> {
    const UsdType& operator()(const void* data) const
    {
        return *reinterpret_cast<const UsdType*>(data);
    }
};

// Conversion by construction.
template <class UsdType, class AlembicType>
struct _ConvertPODToUsd<UsdType, AlembicType, 1> {
    UsdType operator()(const void* data) const
    {
        return UsdType(*reinterpret_cast<const AlembicType*>(data));
    }
};

// Construct vector.
template <class UsdType>
struct _ConvertPODToUsdVec {
    UsdType operator()(const void* data) const
    {
        typedef typename UsdType::ScalarType ScalarType;
        return UsdType(reinterpret_cast<const ScalarType*>(data));
    }
};
template <>
struct _ConvertPODToUsd<GfVec2i, int32_t,   2> : _ConvertPODToUsdVec<GfVec2i>{};
template <>
struct _ConvertPODToUsd<GfVec2h, GfHalf,    2> : _ConvertPODToUsdVec<GfVec2h>{};
template <>
struct _ConvertPODToUsd<GfVec2f, float32_t, 2> : _ConvertPODToUsdVec<GfVec2f>{};
template <>
struct _ConvertPODToUsd<GfVec2d, float64_t, 2> : _ConvertPODToUsdVec<GfVec2d>{};
template <>
struct _ConvertPODToUsd<GfVec3i, int32_t,   3> : _ConvertPODToUsdVec<GfVec3i>{};
template <>
struct _ConvertPODToUsd<GfVec3h, GfHalf,    3> : _ConvertPODToUsdVec<GfVec3h>{};
template <>
struct _ConvertPODToUsd<GfVec3f, float32_t, 3> : _ConvertPODToUsdVec<GfVec3f>{};
template <>
struct _ConvertPODToUsd<GfVec3d, float64_t, 3> : _ConvertPODToUsdVec<GfVec3d>{};
template <>
struct _ConvertPODToUsd<GfVec4i, int32_t,   4> : _ConvertPODToUsdVec<GfVec4i>{};
template <>
struct _ConvertPODToUsd<GfVec4h, GfHalf,    4> : _ConvertPODToUsdVec<GfVec4h>{};
template <>
struct _ConvertPODToUsd<GfVec4f, float32_t, 4> : _ConvertPODToUsdVec<GfVec4f>{};
template <>
struct _ConvertPODToUsd<GfVec4d, float64_t, 4> : _ConvertPODToUsdVec<GfVec4d>{};

// Construct quaternion.
// Note: Imath quaternions are stored as (r, i0, i1, i2) whereas the Gf
// versions are stored as (i0, i1, i2, r)
template <>
struct _ConvertPODToUsd<GfQuatf, float32_t, 4> {
    GfQuatf operator()(const void* data) const
    {
        const float32_t* src = reinterpret_cast<const float32_t*>(data);
        GfVec3f imaginary(src[1], src[2], src[3]);
        return GfQuatf(src[0], imaginary);
    }
};

template <>
struct _ConvertPODToUsd<GfQuatd, float64_t, 4> {
    GfQuatd operator()(const void* data) const
    {
        const float64_t* src = reinterpret_cast<const float64_t*>(data);
        GfVec3d imaginary(src[1], src[2], src[3]);
        return GfQuatd(src[0], imaginary);
    }
};


// Construct matrix.
template <>
struct _ConvertPODToUsd<GfMatrix4d, float32_t, 16> {
    GfMatrix4d operator()(const void* data) const
    {
        float64_t buffer[4][4];
        const float32_t* src = reinterpret_cast<const float32_t*>(data);
        std::copy(src, src + 16, &buffer[0][0]);
        return GfMatrix4d(buffer);
    }
};
template <>
struct _ConvertPODToUsd<GfMatrix4d, float64_t, 16> {
    GfMatrix4d operator()(const void* data) const
    {
        return GfMatrix4d(reinterpret_cast<const float64_t(*)[4]>(data));
    }
};

// Copy an array -- general case.
template <class UsdType, class AlembicType, size_t extent>
struct _ConvertPODToUsdArray {
    void operator()(UsdType* dst, const void* src, size_t size)
    {
        const uint8_t* typedSrc = reinterpret_cast<const uint8_t*>(src);
        const size_t step = extent * sizeof(AlembicType);
        for (size_t i = 0, n = size; i != n; typedSrc += step, ++i) {
            dst[i] = _ConvertPODToUsd<UsdType, AlembicType, extent>()(typedSrc);
        }
    }
};

// Copy an array -- no conversion necessary.
template <class UsdType>
struct _ConvertPODToUsdArray<UsdType, UsdType, 1> {
    void operator()(UsdType* dst, const void* src, size_t size)
    {
        const UsdType* typedSrc = reinterpret_cast<const UsdType*>(src);
        std::copy(typedSrc, typedSrc + size, dst);
    }
};

//
// POD conversion from Usd.
//

template <class UsdType, class AlembicType, size_t extent>
struct _ConvertPODFromUsd { };

// No conversion necessary.
template <class UsdType>
struct _ConvertPODFromUsd<UsdType, UsdType, 1> {
    void operator()(const UsdType& src, UsdType* dst) const
    {
        *dst = src;
    }
};

// Conversion by construction.
template <class UsdType, class AlembicType>
struct _ConvertPODFromUsd<UsdType, AlembicType, 1> {
    void operator()(const UsdType& src, AlembicType* dst) const
    {
        *dst = AlembicType(src);
    }
};

// Conversion for TfToken to std::string.
template <>
struct _ConvertPODFromUsd<TfToken, std::string, 1> {
    void operator()(const TfToken& src, std::string* dst) const
    {
        *dst = src.GetString();
    }
};

// Construct vector.
template <class UsdType>
struct _ConvertPODFromUsdVec {
    void operator()(const UsdType& src, typename UsdType::ScalarType* dst) const
    {
        std::copy(src.GetArray(), src.GetArray() + UsdType::dimension, dst);
    }
};
template <>
struct _ConvertPODFromUsd<GfVec2i, int32_t,   2> :
    _ConvertPODFromUsdVec<GfVec2i> { };
template <>
struct _ConvertPODFromUsd<GfVec2h, GfHalf,    2> :
    _ConvertPODFromUsdVec<GfVec2h> { };
template <>
struct _ConvertPODFromUsd<GfVec2f, float32_t, 2> :
    _ConvertPODFromUsdVec<GfVec2f> { };
template <>
struct _ConvertPODFromUsd<GfVec2d, float64_t, 2> :
    _ConvertPODFromUsdVec<GfVec2d> { };
template <>
struct _ConvertPODFromUsd<GfVec3i, int32_t,   3> :
    _ConvertPODFromUsdVec<GfVec3i> { };
template <>
struct _ConvertPODFromUsd<GfVec3h, GfHalf,    3> :
    _ConvertPODFromUsdVec<GfVec3h> { };
template <>
struct _ConvertPODFromUsd<GfVec3f, float32_t, 3> :
    _ConvertPODFromUsdVec<GfVec3f> { };
template <>
struct _ConvertPODFromUsd<GfVec3d, float64_t, 3> :
    _ConvertPODFromUsdVec<GfVec3d> { };
template <>
struct _ConvertPODFromUsd<GfVec4i, int32_t,   4> :
    _ConvertPODFromUsdVec<GfVec4i> { };
template <>
struct _ConvertPODFromUsd<GfVec4h, GfHalf,    4> :
    _ConvertPODFromUsdVec<GfVec4h> { };
template <>
struct _ConvertPODFromUsd<GfVec4f, float32_t, 4> :
    _ConvertPODFromUsdVec<GfVec4f> { };
template <>
struct _ConvertPODFromUsd<GfVec4d, float64_t, 4> :
    _ConvertPODFromUsdVec<GfVec4d> { };

// Construct quaternion.
// Note: Imath quaternions are stored as (r, i0, i1, i2) whereas the Gf
// versions are stored as (i0, i1, i2, r)
template <>
struct _ConvertPODFromUsd<GfQuatf, float32_t, 4> {
    void operator()(const GfQuatf& src, float32_t* dst) const
    {
        dst[0] = src.GetReal();
        dst[1] = src.GetImaginary()[0];
        dst[2] = src.GetImaginary()[1];
        dst[3] = src.GetImaginary()[2];
    }
};

template <>
struct _ConvertPODFromUsd<GfQuatd, float64_t, 4> {
    void operator()(const GfQuatd& src, float64_t* dst) const
    {
        dst[0] = src.GetReal();
        dst[1] = src.GetImaginary()[0];
        dst[2] = src.GetImaginary()[1];
        dst[3] = src.GetImaginary()[2];
    }
};

// Construct matrix.
template <>
struct _ConvertPODFromUsd<GfMatrix4d, float32_t, 16> {
    void operator()(const GfMatrix4d& src, float32_t* dst) const
    {
        std::copy(src.GetArray(), src.GetArray() + 16, dst);
    }
};
template <>
struct _ConvertPODFromUsd<GfMatrix4d, float64_t, 16> {
    void operator()(const GfMatrix4d& src, float64_t* dst) const
    {
        std::copy(src.GetArray(), src.GetArray() + 16, dst);
    }
};

// Copy a scalar -- general case.
template <class UsdType, class AlembicType, size_t extent>
struct _ConvertPODFromUsdScalar {
    _SampleForAlembic operator()(const VtValue& src) const
    {
        boost::shared_array<AlembicType> dst(new AlembicType[extent]);
        _ConvertPODFromUsd<UsdType, AlembicType, extent>()(
                src.UncheckedGet<UsdType>(), dst.get());
        return _SampleForAlembic(dst, extent);
    }
};

// Copy a scalar -- POD.
template <class UsdType, class AlembicType>
struct _ConvertPODFromUsdScalar<UsdType, AlembicType, 1> {
    _SampleForAlembic operator()(const VtValue& src) const
    {
        AlembicType dst;
        _ConvertPODFromUsd<UsdType, AlembicType, 1>()(
                src.UncheckedGet<UsdType>(), &dst);
        return _SampleForAlembic(dst);
    }
};

// Copy an array -- general case.
template <class UsdType, class AlembicType, size_t extent>
struct _ConvertPODFromUsdArray {
    _SampleForAlembic operator()(const VtValue& src)
    {
        const VtArray<UsdType>& data = src.UncheckedGet<VtArray<UsdType> >();
        const size_t size = data.size();
        boost::shared_array<AlembicType> array(new AlembicType[size * extent]);
        AlembicType* ptr = array.get();
        for (size_t i = 0, n = size; i != n; ptr += extent, ++i) {
            _ConvertPODFromUsd<UsdType, AlembicType, extent>()(data[i], ptr);
        }
        return _SampleForAlembic(array, size * extent);
    }
};

//
// Alembic <-> Usd conversion registries.
//

template <class UsdType, class AlembicType, size_t extent>
struct _ConvertPODScalar;
template <class UsdType, class AlembicType, size_t extent>
struct _ConvertPODArray;

/// Holds a dictionary of property value conversions.  It can apply the
/// appropriate conversion to a given property and store the result.
class UsdAbc_AlembicDataConversion {
public:
    /// A conversion function.  Returns \c true on success and copies the
    /// value from the named property in the compound property at the given
    /// sample selector to the \c UsdAbc_AlembicDataAny value.  The converter
    /// can assume the property has the type that the converter was keyed by.
    typedef std::function<bool (const ICompoundProperty&,
                                const std::string&,
                                const ISampleSelector&,
                                const UsdAbc_AlembicDataAny&)> ToUsdConverter;

    /// A reverse conversion function (Usd -> Alembic).  Returns the value
    /// as a \c _SampleForAlembic.  The converter can assume the VtValue
    /// is holding the expected type.
    typedef std::function<_SampleForAlembic(const VtValue&)> FromUsdConverter;

    UsdAbc_AlembicDataConversion();

    /// Register converters between \p UsdType and \p AlembicType with
    /// extent \p extent.  Note that this will not register a conversion
    /// that's already been registered.  This is useful to register a
    /// preferred conversion when there's a many-to-one mapping.
    template <class UsdType, class AlembicType, size_t extent>
    void AddConverter(const SdfValueTypeName& usdType)
    {
        const PlainOldDataType pod = PODTraitsFromType<AlembicType>::pod_enum;
        _AddConverter(UsdAbc_AlembicType(pod, extent, false),
                      usdType,
                      _ConvertPODScalar<UsdType, AlembicType, extent>(),
                      _ConvertPODScalar<UsdType, AlembicType, extent>());
        _AddConverter(UsdAbc_AlembicType(pod, extent, true),
                      usdType.GetArrayType(),
                      _ConvertPODArray<UsdType, AlembicType, extent>(),
                      _ConvertPODArray<UsdType, AlembicType, extent>());
    }

    /// Register converters between \p UsdType and \p AlembicType with
    /// extent \p extent.  Note that this will not register a conversion
    /// that's already been registered.  This is useful to register a
    /// preferred conversion when there's a many-to-one mapping.
    template <class UsdType, class AlembicType, size_t extent>
    void AddConverter()
    {
        AddConverter<UsdType, AlembicType, extent>(
            SdfSchema::GetInstance().FindType(TfType::Find<UsdType>()));
    }

    template <class UsdType, class AlembicType>
    void AddConverter(const SdfValueTypeName& usdType)
    {
        AddConverter<UsdType, AlembicType, 1>(usdType);
    }

    template <class UsdType, class AlembicType>
    void AddConverter()
    {
        AddConverter<UsdType, AlembicType, 1>();
    }

    /// Returns the Usd type we create by default from the Alembic type
    /// \p alembicType.  Returns an empty token if there is no such
    /// converter.
    SdfValueTypeName FindConverter(const UsdAbc_AlembicType& alembicType) const;

    /// Returns a to-Usd converter that is fully specified (exactly matching 
    /// alembicType and usdType).
    const ToUsdConverter& GetToUsdConverter(
        const UsdAbc_AlembicType& alembicType,
        const SdfValueTypeName& usdType) const;

    /// Returns a pointer to the default (reversible) converter for the
    /// given Usd type, or \c NULL if there's no such converter.
    UsdAbc_AlembicType FindConverter(const SdfValueTypeName& usdType) const;

    /// Returns the reverse converter for the given key.
    const FromUsdConverter& GetConverter(const SdfValueTypeName& usdType) const;

private:
    void _AddConverter(const UsdAbc_AlembicType& alembicType,
                       const SdfValueTypeName& usdType,
                       const ToUsdConverter&, const FromUsdConverter&);

private:

    // All the data that we need to convert between usd and abc types.
    struct _ConverterData {
        _ConverterData(const SdfValueTypeName &usdType_,
                       const UsdAbc_AlembicType &abcType_,
                       const ToUsdConverter &toUsdFn_,
                       const FromUsdConverter &fromUsdFn_)
        : usdType(usdType_)
        , abcType(abcType_)
        , toUsdFn(toUsdFn_)
        , fromUsdFn(fromUsdFn_)
        { }

        SdfValueTypeName usdType;
        UsdAbc_AlembicType abcType;
        ToUsdConverter toUsdFn;
        FromUsdConverter fromUsdFn;
    };

    // All the type converters that we have registered.
    std::vector<_ConverterData> _typeConverters;
};

/// All conversions.  This exists so we can construct all conversion types
/// as a single object for convenience with TfStaticData.
struct UsdAbc_AlembicConversions {
    UsdAbc_AlembicConversions();

    UsdAbc_AlembicDataConversion data;
};

//
// Utilities
//

/// Format an Alembic version number as a string.
std::string
UsdAbc_FormatAlembicVersion(int32_t n);

/// Reverse the order of the subsequences in \p values where the subsequence
/// lengths are given by \p counts.
template <class T>
static bool
UsdAbc_ReverseOrderImpl(
    VtArray<T>& values,
    const VtArray<int>& counts)
{
    // Reverse items.
    for (size_t j = 0, n = values.size(),
            k = 0, m = counts.size(); k != m; ++k) {
        const int count = counts[k];

        // Bail out with failure if we run out of items.
        if (!TF_VERIFY(j + count <= n)) {
            return false;
        }

        // Reverse the range.
        std::reverse(values.begin() + j, values.begin() + j + count);

        // Next range.
        j += count;
    }
    return true;
}

} // namespace UsdAbc_AlembicUtil


PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_PLUGIN_USD_ABC_ALEMBIC_UTIL_H
