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
#include "pxr/base/vt/value.h"

#include "pxr/base/vt/typeHeaders.h"
#include "pxr/base/vt/types.h"
#include "pxr/base/vt/dictionary.h"

#include "pxr/base/gf/math.h"
#include "pxr/base/tf/instantiateSingleton.h"
#include "pxr/base/tf/iterator.h"
#include "pxr/base/tf/mallocTag.h"
#include "pxr/base/tf/singleton.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/token.h"

#include <boost/preprocessor.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <tbb/spin_mutex.h>
#include <tbb/concurrent_unordered_map.h>

#include <map>
#include <ostream>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <vector>
#include <cmath>
#include <limits>

using std::map;
using std::string;
using std::type_info;
using std::vector;

PXR_NAMESPACE_OPEN_SCOPE

static_assert(std::is_nothrow_move_constructible<VtValue>::value &&
              std::is_nothrow_move_assignable<VtValue>::value, "");

TF_REGISTRY_FUNCTION(TfType) {
    TfType::Define<VtValue>();
}

template<typename From, typename To>
static inline VtValue _BoostNumericCast(const From x)
{
    try {
        return VtValue(boost::numeric_cast<To>(x));
    } catch (const boost::bad_numeric_cast &) {
        return VtValue();
    }
}

// If the To type has no infinity, simply use boost numeric_cast.
template <typename From, typename To>
static
typename std::enable_if<!std::numeric_limits<To>::has_infinity,VtValue>::type
_NumericCast(VtValue const &val)
{
    return _BoostNumericCast<From, To>(val.UncheckedGet<From>());
}

// If the To type has infinity, we convert values larger than the largest
// finite value that To can take to infinity.
template <typename From, typename To>
static
typename std::enable_if<std::numeric_limits<To>::has_infinity,VtValue>::type
_NumericCast(VtValue const &val)
{
    const From x = val.UncheckedGet<From>();

    // Use 'x == x' to check that x is not NaN.  NaNs don't compare equal to
    // themselves.
    if (x == x) {
        if (x >  std::numeric_limits<To>::max()) {
            return VtValue( std::numeric_limits<To>::infinity());
        }
        if (x < -std::numeric_limits<To>::max()) {
            return VtValue(-std::numeric_limits<To>::infinity());
        }
    }

    return _BoostNumericCast<From, To>(x);
}

template <class A, class B>
static void _RegisterNumericCasts()
{
    VtValue::RegisterCast<A, B>(_NumericCast<A, B>);
    VtValue::RegisterCast<B, A>(_NumericCast<B, A>);
}

class Vt_CastRegistry {

  public:

    static Vt_CastRegistry &GetInstance() {
        return TfSingleton<Vt_CastRegistry>::GetInstance();
    }    

    void Register(type_info const &from,
                  type_info const &to,
                  VtValue (*castFn)(VtValue const &))
    {
        std::type_index src = from;
        std::type_index dst = to;

        bool isNewEntry = _conversions.insert(
            std::make_pair(_ConversionSourceToTarget(src, dst), castFn)).second;
        if (!isNewEntry) {
            // This happens at startup if there's a bug in the code.
            TF_CODING_ERROR("VtValue cast already registered from "
                            "'%s' to '%s'.  New cast will be ignored.",
                            ArchGetDemangled(from).c_str(),
                            ArchGetDemangled(to).c_str());
            return;
        }
    }

    VtValue PerformCast(type_info const &to, VtValue const &val) {
        if (val.IsEmpty())
            return val;

        std::type_index src = val.GetTypeid();
        std::type_index dst = to;

        VtValue (*castFn)(VtValue const &) = NULL;

        _Conversions::iterator c = _conversions.find({src, dst});
        if (c != _conversions.end()) {
            castFn = c->second;
        }
        return castFn ? castFn(val) : VtValue();
    }

    bool CanCast(type_info const &from, type_info const &to) {
        std::type_index src = from;
        std::type_index dst = to;

        return _conversions.find({src, dst}) != _conversions.end();
    }

  private:
    Vt_CastRegistry() {
        TfSingleton<Vt_CastRegistry>::SetInstanceConstructed(*this);
        _RegisterBuiltinCasts();
        TfRegistryManager::GetInstance().SubscribeTo<VtValue>();
    }
    virtual ~Vt_CastRegistry() {}
    friend class TfSingleton<Vt_CastRegistry>;

    // Disambiguate TfToken->string conversion
    static VtValue _TfTokenToString(VtValue const &val) {
        return VtValue(val.UncheckedGet<TfToken>().GetString());
    }
    static VtValue _TfStringToToken(VtValue const &val) {
        return VtValue(TfToken(val.UncheckedGet<std::string>()));
    }

    void _RegisterBuiltinCasts() {
        _RegisterNumericCasts<bool, char>();
        _RegisterNumericCasts<bool, signed char>();
        _RegisterNumericCasts<bool, unsigned char>();
        _RegisterNumericCasts<bool, short>();
        _RegisterNumericCasts<bool, unsigned short>();
        _RegisterNumericCasts<bool, int>();
        _RegisterNumericCasts<bool, unsigned int>();
        _RegisterNumericCasts<bool, long>();
        _RegisterNumericCasts<bool, unsigned long>();
        _RegisterNumericCasts<bool, long long>();
        _RegisterNumericCasts<bool, unsigned long long>();
        _RegisterNumericCasts<bool, GfHalf>();
        _RegisterNumericCasts<bool, float>();
        _RegisterNumericCasts<bool, double>();

        _RegisterNumericCasts<char, signed char>();
        _RegisterNumericCasts<char, unsigned char>();
        _RegisterNumericCasts<char, short>();
        _RegisterNumericCasts<char, unsigned short>();
        _RegisterNumericCasts<char, int>();
        _RegisterNumericCasts<char, unsigned int>();
        _RegisterNumericCasts<char, long>();
        _RegisterNumericCasts<char, unsigned long>();
        _RegisterNumericCasts<char, long long>();
        _RegisterNumericCasts<char, unsigned long long>();
        _RegisterNumericCasts<char, GfHalf>();
        _RegisterNumericCasts<char, float>();
        _RegisterNumericCasts<char, double>();

        _RegisterNumericCasts<signed char, unsigned char>();
        _RegisterNumericCasts<signed char, short>();
        _RegisterNumericCasts<signed char, unsigned short>();
        _RegisterNumericCasts<signed char, int>();
        _RegisterNumericCasts<signed char, unsigned int>();
        _RegisterNumericCasts<signed char, long>();
        _RegisterNumericCasts<signed char, unsigned long>();
        _RegisterNumericCasts<signed char, long long>();
        _RegisterNumericCasts<signed char, unsigned long long>();
        _RegisterNumericCasts<signed char, GfHalf>();
        _RegisterNumericCasts<signed char, float>();
        _RegisterNumericCasts<signed char, double>();

        _RegisterNumericCasts<unsigned char, short>();
        _RegisterNumericCasts<unsigned char, unsigned short>();
        _RegisterNumericCasts<unsigned char, int>();
        _RegisterNumericCasts<unsigned char, unsigned int>();
        _RegisterNumericCasts<unsigned char, long>();
        _RegisterNumericCasts<unsigned char, unsigned long>();
        _RegisterNumericCasts<unsigned char, long long>();
        _RegisterNumericCasts<unsigned char, unsigned long long>();
        _RegisterNumericCasts<unsigned char, GfHalf>();
        _RegisterNumericCasts<unsigned char, float>();
        _RegisterNumericCasts<unsigned char, double>();

        _RegisterNumericCasts<short, unsigned short>();
        _RegisterNumericCasts<short, int>();
        _RegisterNumericCasts<short, unsigned int>();
        _RegisterNumericCasts<short, long>();
        _RegisterNumericCasts<short, unsigned long>();
        _RegisterNumericCasts<short, long long>();
        _RegisterNumericCasts<short, unsigned long long>();
        _RegisterNumericCasts<short, GfHalf>();
        _RegisterNumericCasts<short, float>();
        _RegisterNumericCasts<short, double>();

        _RegisterNumericCasts<unsigned short, int>();
        _RegisterNumericCasts<unsigned short, unsigned int>();
        _RegisterNumericCasts<unsigned short, long>();
        _RegisterNumericCasts<unsigned short, unsigned long>();
        _RegisterNumericCasts<unsigned short, long long>();
        _RegisterNumericCasts<unsigned short, unsigned long long>();
        _RegisterNumericCasts<unsigned short, GfHalf>();
        _RegisterNumericCasts<unsigned short, float>();
        _RegisterNumericCasts<unsigned short, double>();

        _RegisterNumericCasts<int, unsigned int>();
        _RegisterNumericCasts<int, long>();
        _RegisterNumericCasts<int, unsigned long>();
        _RegisterNumericCasts<int, long long>();
        _RegisterNumericCasts<int, unsigned long long>();
        _RegisterNumericCasts<int, GfHalf>();
        _RegisterNumericCasts<int, float>();
        _RegisterNumericCasts<int, double>();

        _RegisterNumericCasts<unsigned int, long>();
        _RegisterNumericCasts<unsigned int, unsigned long>();
        _RegisterNumericCasts<unsigned int, long long>();
        _RegisterNumericCasts<unsigned int, unsigned long long>();
        _RegisterNumericCasts<unsigned int, GfHalf>();
        _RegisterNumericCasts<unsigned int, float>();
        _RegisterNumericCasts<unsigned int, double>();

        _RegisterNumericCasts<long, unsigned long>();
        _RegisterNumericCasts<long, long long>();
        _RegisterNumericCasts<long, unsigned long long>();
        _RegisterNumericCasts<long, GfHalf>();
        _RegisterNumericCasts<long, float>();
        _RegisterNumericCasts<long, double>();

        _RegisterNumericCasts<unsigned long, long long>();
        _RegisterNumericCasts<unsigned long, unsigned long long>();
        _RegisterNumericCasts<unsigned long, GfHalf>();
        _RegisterNumericCasts<unsigned long, float>();
        _RegisterNumericCasts<unsigned long, double>();

        _RegisterNumericCasts<long long, unsigned long long>();
        _RegisterNumericCasts<long long, GfHalf>();
        _RegisterNumericCasts<long long, float>();
        _RegisterNumericCasts<long long, double>();

        _RegisterNumericCasts<unsigned long long, GfHalf>();
        _RegisterNumericCasts<unsigned long long, float>();
        _RegisterNumericCasts<unsigned long long, double>();

        _RegisterNumericCasts<GfHalf, float>();
        _RegisterNumericCasts<GfHalf, double>();

        _RegisterNumericCasts<float, double>();

        VtValue::RegisterCast<TfToken, std::string>(_TfTokenToString);
        VtValue::RegisterCast<std::string, TfToken>(_TfStringToToken);
    } 

    using _ConversionSourceToTarget =
        std::pair<std::type_index, std::type_index>;

    struct _ConversionSourceToTargetHash
    {
        std::size_t operator()(_ConversionSourceToTarget p) const
        {
            std::size_t h = p.first.hash_code();
            boost::hash_combine(h, p.second.hash_code());
            return h;
        }
    };

    using _Conversions = tbb::concurrent_unordered_map<
        _ConversionSourceToTarget,
        VtValue (*)(VtValue const &),
        _ConversionSourceToTargetHash>;

    _Conversions _conversions;
    
};
TF_INSTANTIATE_SINGLETON(Vt_CastRegistry);


// Force instantiation for the registry instance.
ARCH_CONSTRUCTOR(Vt_CastRegistryInit, 255)
{
    Vt_CastRegistry::GetInstance();
}

bool
VtValue::IsArrayValued() const {
    VtValue const *v = _ResolveProxy();
    return v->_info.GetLiteral() && v->_info.Get()->isArray;
}

const Vt_ShapeData*
VtValue::_GetShapeData() const
{
    VtValue const *v = _ResolveProxy();
    return v->_info.GetLiteral() ?
        v->_info.Get()->GetShapeData(v->_storage) : nullptr;
}

size_t
VtValue::_GetNumElements() const
{
    VtValue const *v = _ResolveProxy();
    return v->_info.GetLiteral() ?
        v->_info.Get()->GetNumElements(v->_storage) : 0;
}

std::type_info const &
VtValue::GetTypeid() const {
    VtValue const *v = _ResolveProxy();
    return v->_info.GetLiteral() ?
        v->_info.Get()->typeInfo : typeid(void);
}

std::type_info const &
VtValue::GetElementTypeid() const {
    VtValue const *v = _ResolveProxy();
    return v->_info.GetLiteral() ?
        v->_info.Get()->elementTypeInfo : typeid(void);
}

TfType
VtValue::GetType() const
{
    if (ARCH_UNLIKELY(_IsProxy()))
        return _info.Get()->GetProxiedType(_storage);

    TfType t = TfType::Find(GetTypeid());
    if (t.IsUnknown()) {
        TF_WARN("Returning unknown type for VtValue with unregistered "
                "C++ type %s", ArchGetDemangled(GetTypeid()).c_str());
    }
    return t;
}

std::string
VtValue::GetTypeName() const
{
    if (ARCH_UNLIKELY(_IsProxy()))
        return GetType().GetTypeName();
    else
        return ArchGetDemangled(GetTypeid());
}

bool
VtValue::CanHash() const
{
    VtValue const *v = _ResolveProxy();
    return v->_info.GetLiteral() && v->_info.Get()->isHashable;
}

size_t
VtValue::GetHash() const {
    if (IsEmpty())
        return 0;
    size_t h = _info->Hash(_storage);
    boost::hash_combine(h, GetTypeid().hash_code());
    return h;
}

/* static */ VtValue
VtValue::CastToTypeOf(VtValue const &val, VtValue const &other) {
    VtValue ret = val;
    return ret.CastToTypeOf(other);
}

/* static */ VtValue
VtValue::CastToTypeid(VtValue const &val, std::type_info const &type) {
    VtValue ret = val;
    return ret.CastToTypeid(type);
}

void VtValue::_RegisterCast(type_info const &from,
                            type_info const &to,
                            VtValue (*castFn)(VtValue const &))
{
    Vt_CastRegistry::GetInstance().Register(from, to, castFn);
}

VtValue VtValue::_PerformCast(type_info const &to, VtValue const &val)
{
    if (TfSafeTypeCompare(val.GetTypeid(), to))
        return val;
    return Vt_CastRegistry::GetInstance().PerformCast(to, val);
}

bool VtValue::_CanCast(type_info const &from, type_info const &to)
{
    if (TfSafeTypeCompare(from, to))
        return true;
    return Vt_CastRegistry::GetInstance().CanCast(from, to);
}

bool
VtValue::_EqualityImpl(VtValue const &rhs) const
{
    // We're guaranteed by the caller that neither *this nor rhs are empty and
    // that _info and rhs._info do not point to the same object.
    if (ARCH_UNLIKELY(_IsProxy() != rhs._IsProxy())) {
        // Either one or the other are proxies, but not both.  Check the types
        // first.  If they match then resolve the proxy and compare with the
        // nonProxy.  This way, proxies are only ever asked to compare to the
        // same proxy type, never to their proxied type.
        if (GetType() != rhs.GetType())
            return false;

        VtValue const *proxy = _IsProxy() ? this : &rhs;
        VtValue const *nonProxy = _IsProxy() ? &rhs : this;
        VtValue const *resolvedProxy = proxy->_ResolveProxy();
        return !resolvedProxy->IsEmpty() &&
            nonProxy->_info->Equal(nonProxy->_storage, resolvedProxy->_storage);
    }

    // Otherwise compare typeids and if they match dispatch to the held type.
    return TfSafeTypeCompare(GetTypeid(), rhs.GetTypeid()) &&
        _info->Equal(_storage, rhs._storage);
}

std::ostream &
operator<<(std::ostream &out, const VtValue &self) {
    return self.IsEmpty() ? out : self._info->StreamOut(self._storage, out);
}

#ifdef PXR_PYTHON_SUPPORT_ENABLED
TfPyObjWrapper
VtValue::_GetPythonObject() const
{
    VtValue const *v = _ResolveProxy();
    return v->_info.GetLiteral() ?
        v->_info.Get()->GetPyObj(v->_storage) : TfPyObjWrapper();
}
#endif // PXR_PYTHON_SUPPORT_ENABLED

static void const *
_FindOrCreateDefaultValue(std::type_info const &type,
                          Vt_DefaultValueHolder (*factory)())
{
    // This function returns a default value for \a type.  It stores a global
    // map from type name to value.  If we have an entry for the requested type
    // in the map already, return that.  Otherwise use \a factory to create a
    // new entry to store in the map, asserting that it produced a value of the
    // correct type.

    TfAutoMallocTag2 tag("Vt", "VtValue _FindOrCreateDefaultValue");
    
    typedef map<string, Vt_DefaultValueHolder> DefaultValuesMap;
    
    static DefaultValuesMap defaultValues;
    static tbb::spin_mutex defaultValuesMutex;

    string key = ArchGetDemangled(type);

    {
        // If there's already an entry for this type we can return it directly.
        tbb::spin_mutex::scoped_lock lock(defaultValuesMutex);
        DefaultValuesMap::iterator i = defaultValues.find(key);
        if (i != defaultValues.end())
            return i->second.GetPointer();
    }

    // We need to make a new entry.  Call the factory function while the mutex
    // is unlocked.  We do this because the factory is unknown code which could
    // plausibly call back into here, causing deadlock.  Assert that the factory
    // produced a value of the correct type.
    Vt_DefaultValueHolder newValue = factory();
    TF_AXIOM(TfSafeTypeCompare(newValue.GetType(), type));

    // We now lock the mutex and attempt to insert the new value.  This may fail
    // if another thread beat us to it while we were creating the new value and
    // weren't holding the lock.  If this happens, we leak the default value we
    // created that isn't used.
    tbb::spin_mutex::scoped_lock lock(defaultValuesMutex);
    DefaultValuesMap::iterator i =
        defaultValues.emplace(std::move(key), std::move(newValue)).first;
    return i->second.GetPointer();
}

bool
VtValue::_TypeIsImpl(std::type_info const &qt) const
{
    if (ARCH_UNLIKELY(_IsProxy())) {
        return _info->ProxyHoldsType(_storage, qt);
    }
    return false;
}

void const *
VtValue::_FailGet(Vt_DefaultValueHolder (*factory)(),
                  std::type_info const &queryType) const
{
    // Issue a coding error detailing relevant types.
    if (IsEmpty()) {
        TF_CODING_ERROR("Attempted to get value of type '%s' from "
                        "empty VtValue.", ArchGetDemangled(queryType).c_str());
    } else {
        TF_CODING_ERROR("Attempted to get value of type '%s' from "
                        "VtValue holding '%s'",
                        ArchGetDemangled(queryType).c_str(),
                        ArchGetDemangled(GetTypeid()).c_str());
    }

    // Get a default value for query type, and use that.
    return _FindOrCreateDefaultValue(queryType, factory);
}

std::ostream &
VtStreamOut(vector<VtValue> const &val, std::ostream &stream) {
    bool first = true;
    stream << '[';
    TF_FOR_ALL(i, val) {
        if (first)
            first = false;
        else
            stream << ", ";
        stream << *i;
    }
    stream << ']';
    return stream;
}    

#define _VT_IMPLEMENT_ZERO_VALUE_FACTORY(r, unused, elem)                \
template <> VT_API                                                       \
Vt_DefaultValueHolder Vt_DefaultValueFactory<VT_TYPE(elem)>::Invoke()    \
{                                                                        \
    return Vt_DefaultValueHolder::Create(VtZero<VT_TYPE(elem)>());       \
}                                                                        \
template struct Vt_DefaultValueFactory<VT_TYPE(elem)>;

BOOST_PP_SEQ_FOR_EACH(_VT_IMPLEMENT_ZERO_VALUE_FACTORY,
                      unused,
                      VT_VEC_VALUE_TYPES
                      VT_MATRIX_VALUE_TYPES
                      VT_QUATERNION_VALUE_TYPES)

PXR_NAMESPACE_CLOSE_SCOPE
