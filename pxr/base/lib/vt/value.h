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
#ifndef VT_VALUE_H
#define VT_VALUE_H

#include "pxr/pxr.h"

#ifdef PXR_PYTHON_SUPPORT_ENABLED
// XXX: Include pyLock.h after pyObjWrapper.h to work around
// Python include ordering issues.
#include "pxr/base/tf/pyObjWrapper.h"
#endif // PXR_PYTHON_SUPPORT_ENABLED

#include "pxr/base/tf/pyLock.h"

#include "pxr/base/arch/demangle.h"
#include "pxr/base/arch/hints.h"
#include "pxr/base/tf/anyUniquePtr.h"
#include "pxr/base/tf/pointerAndBits.h"
#include "pxr/base/tf/safeTypeCompare.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/tf.h"
#include "pxr/base/tf/type.h"

#include "pxr/base/vt/api.h"
#include "pxr/base/vt/hash.h"
#include "pxr/base/vt/streamOut.h"
#include "pxr/base/vt/traits.h"
#include "pxr/base/vt/types.h"

#include <boost/aligned_storage.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/mpl/and.hpp>
#include <boost/mpl/if.hpp>
#include <boost/type_traits/decay.hpp>
#include <boost/type_traits/has_trivial_assign.hpp>
#include <boost/type_traits/has_trivial_constructor.hpp>
#include <boost/type_traits/has_trivial_copy.hpp>
#include <boost/type_traits/has_trivial_destructor.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/utility/enable_if.hpp>

#include <iosfwd>
#include <typeinfo>
#include <type_traits>

PXR_NAMESPACE_OPEN_SCOPE

/// Make a default value.
/// VtValue uses this to create values to be returned from failed calls to \a
/// Get. Clients may specialize this for their own types.
template <class T>
struct Vt_DefaultValueFactory;

// This is a helper class used by Vt_DefaultValueFactory to return a value with
// its type erased and only known at runtime via a std::type_info.
struct Vt_DefaultValueHolder
{
    // Creates a value-initialized object and stores the type_info for the
    // static type.
    template <typename T>
    static Vt_DefaultValueHolder Create() {
        return Vt_DefaultValueHolder(TfAnyUniquePtr::New<T>(), typeid(T));
    }

    // Creates a copy of the object and stores the type_info for the static
    // type.
    template <typename T>
    static Vt_DefaultValueHolder Create(T const &val) {
        return Vt_DefaultValueHolder(TfAnyUniquePtr::New(val), typeid(T));
    }

    // Return the runtime type of the held object.
    std::type_info const &GetType() const {
        return *_type;
    }

    // Return a pointer to the held object.  This may be safely cast to the
    // static type corresponding to the type_info returned by GetType.
    void const *GetPointer() const {
        return _ptr.Get();
    }

private:
    Vt_DefaultValueHolder(TfAnyUniquePtr &&ptr, std::type_info const &type)
        : _ptr(std::move(ptr)), _type(&type) {}

    TfAnyUniquePtr _ptr;
    std::type_info const *_type;
};

class VtValue;

// Overload VtStreamOut for vector<VtValue>.  Produces output like [value1,
// value2, ... valueN].
VT_API std::ostream &VtStreamOut(std::vector<VtValue> const &val, std::ostream &);

// Base implementations for VtGetProxied{Type,Value}.
template <class T>
bool VtProxyHoldsType(T const &, std::type_info const &) { return false; }
template <class T>
TfType VtGetProxiedType(T const &) { return TfType(); }
template <class T>
VtValue const *VtGetProxiedValue(T const &) { return NULL; }

#define VT_VALUE_SET_STORED_TYPE(SRC, DST)                      \
    template <> struct Vt_ValueStoredType<SRC> { typedef DST Type; }

template <class T> struct Vt_ValueStoredType { typedef T Type; };
VT_VALUE_SET_STORED_TYPE(char const *, std::string);
VT_VALUE_SET_STORED_TYPE(char *, std::string);

#ifdef PXR_PYTHON_SUPPORT_ENABLED
VT_VALUE_SET_STORED_TYPE(boost::python::object, TfPyObjWrapper);
#endif // PXR_PYTHON_SUPPORT_ENABLED

#undef VT_VALUE_SET_STORED_TYPE

// A metafunction that gives the type VtValue should store for a given type T.
template <class T>
struct Vt_ValueGetStored 
    : Vt_ValueStoredType<typename boost::decay<T>::type> {};

/// Provides a container which may hold any type, and provides introspection
/// and iteration over array types.  See \a VtIsArray for more info.
///
/// \section VtValue_Casting Held-type Conversion with VtValue::Cast
///
/// VtValue provides a suite of "Cast" methods that convert or create a
/// VtValue holding a requested type (via template parameter, typeid, or
/// type-matching to another VtValue) from the type of the currently-held
/// value.  Clients can add conversions between their own types using the
/// RegisterCast(), RegisterSimpleCast(), and
/// RegisterSimpleBidirectionalCast() methods.  Conversions from plugins can
/// be guaranteed to be registered before they are needed by registering them
/// from within a
/// \code
/// TF_REGISTRY_FUNCTION(VtValue) {
/// }
/// \endcode
/// block.
///
/// \subsection VtValue_builtin_conversions Builtin Type Conversion
///
/// Conversions between most of the basic "value types" that are intrinsically
/// convertible are builtin, including all numeric types (including Gf's \c
/// half), std::string/TfToken, GfVec* (for vecs of the same dimension), and
/// VtArray<T> for floating-point POD and GfVec of the preceding.
///
/// \subsection VtValue_numeric_conversion Numeric Conversion Safety
///
/// The conversions between all scalar numeric types are performed with range
/// checks such as provided by boost::numeric_cast(), and will fail, returning
/// an empty VtValue if the source value is out of range of the destination
/// type.
///
/// Conversions between GfVec and other compound-numeric types provide no more
/// or less safety or checking than the conversion constructors of the types
/// themselves.  This includes VtArray, even VtArray<T> for T in scalar types
/// that are range-checked when held singly.
class VtValue
{
    static const unsigned int _LocalFlag       = 1 << 0;
    static const unsigned int _TrivialCopyFlag = 1 << 1;
    static const unsigned int _ProxyFlag       = 1 << 2;

    template <class T>
    struct _Counted {
        explicit _Counted(T const &obj) : _obj(obj) {
            _refCount = 0;
        }
        bool IsUnique() const { return _refCount == 1; }
        T const &Get() const { return _obj; }
        T &GetMutable() { return _obj; }

    private:
        T _obj;
        mutable std::atomic<int> _refCount;

        friend inline void intrusive_ptr_add_ref(_Counted const *d) {
            d->_refCount.fetch_add(1, std::memory_order_relaxed);
        }
        friend inline void intrusive_ptr_release(_Counted const *d) {
            if (d->_refCount.fetch_sub(1, std::memory_order_release) == 1) {
                std::atomic_thread_fence(std::memory_order_acquire);
                delete d;
            }
        }
    };

    // Hold objects up to 1 word large locally.  This makes the total structure
    // 16 bytes when compiled 64 bit (1 word type-info pointer, 1 word storage
    // space).
    static const size_t _MaxLocalSize = sizeof(void*);
    typedef std::aligned_storage<
        /* size */_MaxLocalSize, /* alignment */_MaxLocalSize>::type _Storage;

    template <class T>
    struct _IsTriviallyCopyable : boost::mpl::and_<
        boost::has_trivial_constructor<T>,
        boost::has_trivial_copy<T>,
        boost::has_trivial_assign<T>,
        boost::has_trivial_destructor<T> > {};

    // Metafunction that returns true if T should be stored locally, false if it
    // should be stored remotely.
    template <class T>
    struct _UsesLocalStore : boost::mpl::bool_<
        (sizeof(T) <= sizeof(_Storage)) &&
        VtValueTypeHasCheapCopy<T>::value &&
        std::is_nothrow_move_constructible<T>::value &&
        std::is_nothrow_move_assignable<T>::value> {};

    // Type information base class.
    struct _TypeInfo {
    private:
        using _CopyInitFunc = void (*)(_Storage const &, _Storage &);
        using _DestroyFunc = void (*)(_Storage &);
        using _MoveFunc = void (*)(_Storage &, _Storage &);
        using _HashFunc = size_t (*)(_Storage const &);
        using _EqualFunc = bool (*)(_Storage const &, _Storage const &);
        using _MakeMutableFunc = void (*)(_Storage &);
#ifdef PXR_PYTHON_SUPPORT_ENABLED
        using _GetPyObjFunc = TfPyObjWrapper (*)(_Storage const &);
#endif // PXR_PYTHON_SUPPORT_ENABLED
        using _StreamOutFunc = std::ostream & (*)(_Storage const &, std::ostream &);
        using _GetShapeDataFunc = const Vt_ShapeData* (*)(_Storage const &);
        using _GetNumElementsFunc = size_t (*)(_Storage const &);
        using _ProxyHoldsTypeFunc = bool (*)(_Storage const &, std::type_info const &);
        using _GetProxiedTypeFunc = TfType (*)(_Storage const &);
        using _GetProxiedValueFunc = VtValue const *(*)(_Storage const &);

    protected:
        constexpr _TypeInfo(const std::type_info &ti,
                            const std::type_info &elementTi,
                            bool isArray,
                            bool isHashable,
                            _CopyInitFunc copyInit,
                            _DestroyFunc destroy,
                            _MoveFunc move,
                            _HashFunc hash,
                            _EqualFunc equal,
                            _MakeMutableFunc makeMutable,
#ifdef PXR_PYTHON_SUPPORT_ENABLED
                            _GetPyObjFunc getPyObj,
#endif // PXR_PYTHON_SUPPORT_ENABLED
                            _StreamOutFunc streamOut,
                            _GetShapeDataFunc getShapeData,
                            _GetNumElementsFunc getNumElements,
                            _ProxyHoldsTypeFunc proxyHoldsType,
                            _GetProxiedTypeFunc getProxiedType,
                            _GetProxiedValueFunc getProxiedValue)
            : typeInfo(ti)
            , elementTypeInfo(elementTi)
            , isArray(isArray)
            , isHashable(isHashable)
            // Function table
            , _copyInit(copyInit)
            , _destroy(destroy)
            , _move(move)
            , _hash(hash)
            , _equal(equal)
            , _makeMutable(makeMutable)
#ifdef PXR_PYTHON_SUPPORT_ENABLED
            , _getPyObj(getPyObj)
#endif // PXR_PYTHON_SUPPORT_ENABLED
            , _streamOut(streamOut)
            , _getShapeData(getShapeData)
            , _getNumElements(getNumElements)
            , _proxyHoldsType(proxyHoldsType)
            , _getProxiedType(getProxiedType)
            , _getProxiedValue(getProxiedValue)
        {}

    public:
        void CopyInit(_Storage const &src, _Storage &dst) const {
            _copyInit(src, dst);
        }
        void Destroy(_Storage &storage) const {
            _destroy(storage);
        }
        void Move(_Storage &src, _Storage &dst) const noexcept {
            _move(src, dst);
        }
        size_t Hash(_Storage const &storage) const {
            return _hash(storage);
        }
        bool Equal(_Storage const &lhs, _Storage const &rhs) const {
            return _equal(lhs, rhs);
        }
        void MakeMutable(_Storage &storage) const {
            _makeMutable(storage);
        }
#ifdef PXR_PYTHON_SUPPORT_ENABLED
        TfPyObjWrapper GetPyObj(_Storage const &storage) const {
            return _getPyObj(storage);
        }
#endif // PXR_PYTHON_SUPPORT_ENABLED
        std::ostream & StreamOut(_Storage const &storage,
                                 std::ostream &out) const {
            return _streamOut(storage, out);
        }
        const Vt_ShapeData* GetShapeData(_Storage const &storage) const {
            return _getShapeData(storage);
        }
        size_t GetNumElements(_Storage const &storage) const {
            return _getNumElements(storage);
        }
        bool ProxyHoldsType(_Storage const &storage,
                            std::type_info const &t) const {
            return _proxyHoldsType(storage, t);
        }
        TfType GetProxiedType(_Storage const &storage) const {
            return _getProxiedType(storage);
        }
        VtValue const *GetProxiedValue(_Storage const &storage) const {
            return _getProxiedValue(storage);
        }

        const std::type_info &typeInfo;
        const std::type_info &elementTypeInfo;
        bool isArray;
        bool isHashable;

    private:
        _CopyInitFunc _copyInit;
        _DestroyFunc _destroy;
        _MoveFunc _move;
        _HashFunc _hash;
        _EqualFunc _equal;
        _MakeMutableFunc _makeMutable;
#ifdef PXR_PYTHON_SUPPORT_ENABLED
        _GetPyObjFunc _getPyObj;
#endif // PXR_PYTHON_SUPPORT_ENABLED
        _StreamOutFunc _streamOut;
        _GetShapeDataFunc _getShapeData;
        _GetNumElementsFunc _getNumElements;
        _ProxyHoldsTypeFunc _proxyHoldsType;
        _GetProxiedTypeFunc _getProxiedType;
        _GetProxiedValueFunc _getProxiedValue;
    };

    // Type-dispatching overloads.

    // Array type helper.
    template <class T, class Enable=void>
    struct _ArrayHelper {
        static const Vt_ShapeData* GetShapeData(T const &) { return NULL; }
        static size_t GetNumElements(T const &) { return 0; }
        constexpr static std::type_info const &GetElementTypeid() { return typeid(void); }
    };
    template <class Array>
    struct _ArrayHelper<Array,
                        typename boost::enable_if<VtIsArray<Array> >::type> {
        static const Vt_ShapeData* GetShapeData(Array const &obj) {
            return obj._GetShapeData();
        }
        static size_t GetNumElements(Array const &obj) {
            return obj.size();
        }
        constexpr static std::type_info const &GetElementTypeid() {
            return typeid(typename Array::ElementType);
        }
    };

    // _TypeInfo implementation helper.  This is a CRTP base that the
    // _LocalTypeInfo and _RemoteTypeInfo types derive.  It wraps their
    // type-specific implementations with type-generic interfaces.
    template <class T, class Container, class Derived>
    struct _TypeInfoImpl : public _TypeInfo
    {
        static const bool IsLocal = _UsesLocalStore<T>::value;
        static const bool HasTrivialCopy = _IsTriviallyCopyable<T>::value;
        static const bool IsProxy = VtIsValueProxy<T>::value;

        using This = _TypeInfoImpl;

        constexpr _TypeInfoImpl()
            : _TypeInfo(typeid(T),
                        _ArrayHelper<T>::GetElementTypeid(),
                        VtIsArray<T>::value,
                        VtIsHashable<T>(),
                        &This::_CopyInit,
                        &This::_Destroy,
                        &This::_Move,
                        &This::_Hash,
                        &This::_Equal,
                        &This::_MakeMutable,
#ifdef PXR_PYTHON_SUPPORT_ENABLED
                        &This::_GetPyObj,
#endif // PXR_PYTHON_SUPPORT_ENABLED
                        &This::_StreamOut,
                        &This::_GetShapeData,
                        &This::_GetNumElements,
                        &This::_ProxyHoldsType,
                        &This::_GetProxiedType,
                        &This::_GetProxiedValue)
        {}

        ////////////////////////////////////////////////////////////////////
        // Typed API for client use.
        static T const &GetObj(_Storage const &storage) {
            return Derived::_GetObj(_Container(storage));
        }

        static T &GetMutableObj(_Storage &storage) {
            return Derived::_GetMutableObj(_Container(storage));
        }

        static void CopyInitObj(T const &objSrc, _Storage &dst) {
            Derived::_PlaceCopy(&_Container(dst), objSrc);
        }

    private:
        static_assert(sizeof(Container) <= sizeof(_Storage),
                      "Container size cannot exceed storage size.");

        ////////////////////////////////////////////////////////////////////
        // _TypeInfo interface function implementations.
        static void _CopyInit(_Storage const &src, _Storage &dst) {
            new (&_Container(dst)) Container(_Container(src));
        }

        static void _Destroy(_Storage &storage) {
            _Container(storage).~Container();
        }

        static size_t _Hash(_Storage const &storage) {
            return VtHashValue(GetObj(storage));
        }

        static bool _Equal(_Storage const &lhs, _Storage const &rhs) {
            // Equal is only ever invoked with an object of this specific type.
            // That is, we only ever ask a proxy to compare to a proxy; we never
            // ask a proxy to compare to the proxied object.
            return GetObj(lhs) == GetObj(rhs);
        }

        static void _Move(_Storage &src, _Storage &dst) noexcept {
            new (&_Container(dst)) Container(std::move(_Container(src)));
            _Destroy(src);
        }

        static void _MakeMutable(_Storage &storage) {
            GetMutableObj(storage);
        }

#ifdef PXR_PYTHON_SUPPORT_ENABLED
        static TfPyObjWrapper _GetPyObj(_Storage const &storage) {
            TfPyLock lock;
            return boost::python::api::object(GetObj(storage));
        }
#endif // PXR_PYTHON_SUPPORT_ENABLED

        static std::ostream &_StreamOut(
            _Storage const &storage, std::ostream &out) {
            return VtStreamOut(GetObj(storage), out);
        }

        static const Vt_ShapeData* _GetShapeData(_Storage const &storage) {
            return _ArrayHelper<T>::GetShapeData(GetObj(storage));
        }

        static size_t _GetNumElements(_Storage const &storage) {
            return _ArrayHelper<T>::GetNumElements(GetObj(storage));
        }

        static bool
        _ProxyHoldsType(_Storage const &storage, std::type_info const &t) {
            return VtProxyHoldsType(GetObj(storage), t);
        }

        static TfType
        _GetProxiedType(_Storage const &storage) {
            return VtGetProxiedType(GetObj(storage));
        }

        static VtValue const *
        _GetProxiedValue(_Storage const &storage) {
            return VtGetProxiedValue(GetObj(storage));
        }

        ////////////////////////////////////////////////////////////////////
        // Internal helper -- cast type-generic storage to type-specific
        // container.
        static Container &_Container(_Storage &storage) {
            return *((Container *)&storage);
        }
        static Container const &_Container(_Storage const &storage) {
            return *((Container const *)&storage);
        }
    };

    ////////////////////////////////////////////////////////////////////////
    // Local-storage type info implementation.  The container and the object are
    // the same -- there is no distinct container.
    template <class T>
    struct _LocalTypeInfo : _TypeInfoImpl<
        T,                 // type
        T,                 // container
        _LocalTypeInfo<T>  // CRTP
        >
    {
        constexpr _LocalTypeInfo()
            : _TypeInfoImpl<T, T, _LocalTypeInfo<T>>()
        {}

        // Get returns object directly.
        static T &_GetMutableObj(T &obj) { return obj; }
        static T const &_GetObj(T const &obj) { return obj; }
        // Place placement new's object directly.
        static void _PlaceCopy(T *dst, T const &src) { new (dst) T(src); }
    };

    ////////////////////////////////////////////////////////////////////////
    // Remote-storage type info implementation.  The container is an
    // intrusive_ptr to an object holder: _Counted<T>.
    template <class T>
    struct _RemoteTypeInfo : _TypeInfoImpl<
        T,                                  // type
        boost::intrusive_ptr<_Counted<T> >, // container
        _RemoteTypeInfo<T>                  // CRTP
        >
    {
        constexpr _RemoteTypeInfo()
            : _TypeInfoImpl<
                  T, boost::intrusive_ptr<_Counted<T>>, _RemoteTypeInfo<T>>()
        {}

        typedef boost::intrusive_ptr<_Counted<T> > Ptr;
        // Get returns object stored in the pointed-to _Counted<T>.
        static T &_GetMutableObj(Ptr &ptr) {
            if (!ptr->IsUnique())
                ptr.reset(new _Counted<T>(ptr->Get()));
            return ptr->GetMutable();
        }
        static T const &_GetObj(Ptr const &ptr) { return ptr->Get(); }
        // PlaceCopy() allocates a new _Counted<T> with a copy of the object.
        static void _PlaceCopy(Ptr *dst, T const &src) {
            new (dst) Ptr(new _Counted<T>(src));
        }
    };

    // Metafunction that returns the specific _TypeInfo subclass for T.
    template <class T>
    struct _TypeInfoFor {
        // return _UsesLocalStore(T) ? _LocalTypeInfo<T> : _RemoteTypeInfo<T>;
        typedef typename boost::mpl::if_<_UsesLocalStore<T>,
                                         _LocalTypeInfo<T>,
                                         _RemoteTypeInfo<T> >::type Type;
    };

    // Make sure char[N] is treated as a string.
    template <size_t N>
    struct _TypeInfoFor<char[N]> {
        // return _UsesLocalStore(T) ? _LocalTypeInfo<T> : _RemoteTypeInfo<T>;
        typedef typename boost::mpl::if_<_UsesLocalStore<std::string>,
                                         _LocalTypeInfo<std::string>,
                                         _RemoteTypeInfo<std::string> >::type Type;
    };

    // Runtime function to return a _TypeInfo base pointer to a specific
    // _TypeInfo subclass for type T.
    template <class T>
    static TfPointerAndBits<const _TypeInfo> GetTypeInfo() {
        typedef typename _TypeInfoFor<T>::Type TI;
        static const TI ti;
        static constexpr unsigned int flags =
                       (TI::IsLocal ? _LocalFlag : 0) |
                       (TI::HasTrivialCopy ? _TrivialCopyFlag : 0) |
                       (TI::IsProxy ? _ProxyFlag : 0);
        return TfPointerAndBits<const _TypeInfo>(&ti, flags);
    }

    // A helper that moves a held value to temporary storage, but keeps it alive
    // until the _HoldAside object is destroyed.  This is used when assigning
    // over a VtValue that might own the object being assigned.  For instance,
    // if I have a VtValue holding a map<string, VtValue>, and I reassign this
    // VtValue with one of the elements from the map, we must ensure that the
    // map isn't destroyed until after the assignment has taken place.
    friend struct _HoldAside;
    struct _HoldAside {
        explicit _HoldAside(VtValue *val)
            : info((val->IsEmpty() || val->_IsLocalAndTriviallyCopyable())
                   ? static_cast<_TypeInfo const *>(NULL) : val->_info.Get()) {
            if (info)
                info->Move(val->_storage, storage);
        }
        ~_HoldAside() {
            if (info)
                info->Destroy(storage);
        }
        _Storage storage;
        _TypeInfo const *info;
    };

    template <class T>
    typename boost::enable_if<
        boost::is_same<T, typename Vt_ValueGetStored<T>::Type> >::type
    _Init(T const &obj) {
        _info = GetTypeInfo<T>();
        typedef typename _TypeInfoFor<T>::Type TypeInfo;
        TypeInfo::CopyInitObj(obj, _storage);
    }

    template <class T>
    typename boost::disable_if<
        boost::is_same<T, typename Vt_ValueGetStored<T>::Type> >::type
    _Init(T const &obj) {
        _Init(typename Vt_ValueGetStored<T>::Type(obj));
    }

public:

    /// Default ctor gives empty VtValue.
    VtValue() {}

    /// Copy construct with \p other.
    VtValue(VtValue const &other) {
        _Copy(other, *this);
    }

    /// Move construct with \p other.
    VtValue(VtValue &&other) noexcept {
        _Move(other, *this);
    }

    /// Construct a VtValue holding a copy of \p obj.
    /// 
    /// If T is a char pointer or array, produce a VtValue holding a
    /// std::string. If T is boost::python::object, produce a VtValue holding
    /// a TfPyObjWrapper.
    template <class T>
    explicit VtValue(T const &obj) {
        _Init(obj);
    }

    /// Create a new VtValue, taking its contents from \p obj.
    /// 
    /// This is equivalent to creating a VtValue holding a value-initialized
    /// \p T instance, then invoking swap(<held-value>, obj), leaving obj in a
    /// default-constructed (value-initialized) state.  In the case that \p
    /// obj is expensive to copy, it may be significantly faster to use this
    /// idiom when \p obj need not retain its contents:
    ///
    /// \code
    /// MyExpensiveObject obj = CreateObject();
    /// return VtValue::Take(obj);
    /// \endcode
    ///
    /// Rather than:
    ///
    /// \code
    /// MyExpensiveObject obj = CreateObject();
    /// return VtValue(obj);
    /// \endcode
    template <class T>
    static VtValue Take(T &obj) {
        VtValue ret;
        ret.Swap(obj);
        return ret;
    }

    /// Destructor.
    ~VtValue() { _Clear(); }

    /// Copy assignment from another \a VtValue.
    VtValue &operator=(VtValue const &other) {
        if (ARCH_LIKELY(this != &other))
            _Copy(other, *this);
        return *this;
    }

    /// Move assignment from another \a VtValue.
    VtValue &operator=(VtValue &&other) noexcept {
        if (ARCH_LIKELY(this != &other))
            _Move(other, *this);
        return *this;
    }

    /// Assignment operator from any type.
    template <class T>
    inline
    typename boost::enable_if_c<
        _TypeInfoFor<T>::Type::IsLocal &&
        _TypeInfoFor<T>::Type::HasTrivialCopy,
    VtValue &>::type
    operator=(T obj) {
        _Clear();
        _Init(obj);
        return *this;
    }

    /// Assignment operator from any type.
    template <class T>
    typename boost::disable_if_c<
        _TypeInfoFor<T>::Type::IsLocal &&
        _TypeInfoFor<T>::Type::HasTrivialCopy,
    VtValue &>::type
    operator=(T const &obj) {
        _HoldAside tmp(this);
        _Init(obj);
        return *this;
    }

    /// Assigning a char const * gives a VtValue holding a std::string.
    VtValue &operator=(char const *cstr) {
        std::string tmp(cstr);
        _Clear();
        _Init(tmp);
        return *this;
    }

    /// Assigning a char * gives a VtValue holding a std::string.
    VtValue &operator=(char *cstr) {
        return *this = const_cast<char const *>(cstr);
    }

    /// Swap this with \a rhs.
    VtValue &Swap(VtValue &rhs) noexcept {
        // Do nothing if both empty.  Otherwise general swap.
        if (!IsEmpty() || !rhs.IsEmpty()) {
            VtValue tmp;
            _Move(*this, tmp);
            _Move(rhs, *this);
            _Move(tmp, rhs);
        }
        return *this;
    }

    /// Overloaded swap() for generic code/stl/etc.
    friend void swap(VtValue &lhs, VtValue &rhs) { lhs.Swap(rhs); }

    /// Swap the held value with \a rhs.  If this value is holding a T,
    // make an unqualified call to swap(<held-value>, rhs).  If this value is
    // not holding a T, replace the held value with a value-initialized T
    // instance first, then swap.
    template <class T>
    typename boost::enable_if<
        boost::is_same<T, typename Vt_ValueGetStored<T>::Type> >::type
    Swap(T &rhs) {
        if (!IsHolding<T>())
            *this = T();
        UncheckedSwap(rhs);
    }

    /// Swap the held value with \a rhs.  This VtValue must be holding an
    /// object of type \p T.  If it does not, this invokes undefined behavior.
    /// Use Swap() if this VtValue is not known to contain an object of type
    /// \p T.
    template <class T>
    typename boost::enable_if<
        boost::is_same<T, typename Vt_ValueGetStored<T>::Type> >::type
    UncheckedSwap(T &rhs) {
        using std::swap;
        swap(_GetMutable<T>(), rhs);
    }

    /// \overload
    void UncheckedSwap(VtValue &rhs) { Swap(rhs); }

    /// Make this value empty and return the held \p T instance.  If
    /// this value does not hold a \p T instance, make this value empty and
    /// return a default-constructed \p T.
    template <class T>
    T Remove() {
        T result;
        Swap(result);
        _Clear();
        return result;
    }

    /// Make this value empty and return the held \p T instance.  If this
    /// value does not hold a \p T instance, this method invokes undefined
    /// behavior.
    template <class T>
    T UncheckedRemove() {
        T result;
        UncheckedSwap(result);
        _Clear();
        return result;
    }

    /// Return true if this value is holding an object of type \p T, false
    /// otherwise.
    template <class T>
    bool IsHolding() const {
        return _info.GetLiteral() && _TypeIs<T>();
    }

    /// Returns true iff this is holding an array type (see VtIsArray<>).
    VT_API bool IsArrayValued() const;

    /// Return the number of elements in the held value if IsArrayValued(),
    /// return 0 otherwise.
    size_t GetArraySize() const { return _GetNumElements(); }

    /// Returns the typeid of the type held by this value.
    VT_API std::type_info const &GetTypeid() const;

    /// Return the typeid of elements in a array valued type.  If not
    /// holding an array valued type, return typeid(void).
    VT_API std::type_info const &GetElementTypeid() const;

    /// Returns the TfType of the type held by this value.
    VT_API TfType GetType() const;

    /// Return the type name of the held typeid.
    VT_API std::string GetTypeName() const;

    /// Returns a const reference to the held object if the held object
    /// is of type \a T.  Invokes undefined behavior otherwise.  This is the
    /// fastest \a Get() method to use after a successful \a IsHolding() check.
    template <class T>
    T const &UncheckedGet() const { return _Get<T>(); }

    /// Returns a const reference to the held object if the held object
    /// is of type \a T.  Issues an error and returns a const reference to a
    /// default value if the held object is not of type \a T.  Use \a IsHolding
    /// to verify correct type before calling this function.  The default value
    /// returned in case of type mismatch is constructed using
    /// Vt_DefaultValueFactory<T>.  That may be specialized for client types.
    /// The default implementation of the default value factory produces a
    /// value-initialized T.
    template <class T>
    T const &Get() const {
        typedef Vt_DefaultValueFactory<T> Factory;

        // In the unlikely case that the types don't match, we obtain a default
        // value to return and issue an error via _FailGet.
        if (ARCH_UNLIKELY(!IsHolding<T>())) {
            return *(static_cast<T const *>(
                         _FailGet(Factory::Invoke, typeid(T))));
        }

        return _Get<T>();
    }

    /// Return a copy of the held object if the held object is of type T.
    /// Return a copy of the default value \a def otherwise.  Note that this
    /// always returns a copy, as opposed to \a Get() which always returns a
    /// reference.
    template <class T>
    T GetWithDefault(T const &def = T()) const {
        return IsHolding<T>() ? UncheckedGet<T>() : def;
    }

    /// Register a cast from VtValue holding From to VtValue holding To.
    template <typename From, typename To>
    static void RegisterCast(VtValue (*castFn)(VtValue const &)) {
        _RegisterCast(typeid(From), typeid(To), castFn);
    }

    /// Register a simple cast from VtValue holding From to VtValue
    // holding To.
    template <typename From, typename To>
    static void RegisterSimpleCast() {
        _RegisterCast(typeid(From), typeid(To), _SimpleCast<From, To>);
    }

    /// Register a two-way cast from VtValue holding From to VtValue
    /// holding To.
    template <typename From, typename To>
    static void RegisterSimpleBidirectionalCast() {
        RegisterSimpleCast<From, To>();
        RegisterSimpleCast<To, From>();
    }

    /// Return a VtValue holding \c val cast to hold T.  Return empty VtValue
    /// if cast fails.
    ///
    /// This Cast() function is safe to call in multiple threads as it does
    /// not mutate the operant \p val.
    ///
    /// \sa \ref VtValue_Casting
    template <typename T>
    static VtValue Cast(VtValue const &val) {
        VtValue ret = val;
        return ret.Cast<T>();
    }

    /// Return a VtValue holding \c val cast to same type that \c other is
    /// holding.  Return empty VtValue if cast fails.
    ///
    /// This Cast() function is safe to call in multiple threads as it does not
    /// mutate the operant \p val.
    ///
    /// \sa \ref VtValue_Casting
    VT_API static VtValue
    CastToTypeOf(VtValue const &val, VtValue const &other);

    /// Return a VtValue holding \a val cast to \a type.  Return empty VtValue
    /// if cast fails.
    ///
    /// This Cast() function is safe to call in multiple threads as it does not
    /// mutate the operant \p val.
    ///
    /// \sa \ref VtValue_Casting
    VT_API static VtValue
    CastToTypeid(VtValue const &val, std::type_info const &type);

    /// Return if a value of type \a from can be cast to type \a to.
    ///
    /// \sa \ref VtValue_Casting
    static bool CanCastFromTypeidToTypeid(std::type_info const &from,
                                          std::type_info const &to) {
        return _CanCast(from, to);
    }

    /// Return \c this holding value type cast to T.  This value is left
    /// empty if the cast fails.
    ///
    /// \note Since this method mutates this value, it is not safe to invoke on
    /// the same VtValue in multiple threads simultaneously.
    ///
    /// \sa \ref VtValue_Casting
    template <typename T>
    VtValue &Cast() {
        if (IsHolding<T>())
            return *this;
        return *this = _PerformCast(typeid(T), *this);
    }

    /// Return \c this holding value type cast to same type that
    /// \c other is holding.  This value is left empty if the cast fails.
    ///
    /// \note Since this method mutates this value, it is not safe to invoke on
    /// the same VtValue in multiple threads simultaneously.
    ///
    /// \sa \ref VtValue_Casting
    VtValue &CastToTypeOf(VtValue const &other) {
        return *this = _PerformCast(other.GetTypeid(), *this);
    }

    /// Return \c this holding value type cast to \a type.  This value is
    /// left empty if the cast fails.
    ///
    /// \note Since this method mutates this value, it is not safe to invoke on
    /// the same VtValue in multiple threads simultaneously.
    ///
    /// \sa \ref VtValue_Casting
    VtValue &CastToTypeid(std::type_info const &type) {
        return *this = _PerformCast(type, *this);
    }

    /// Return if \c this can be cast to \a T.
    ///
    /// \sa \ref VtValue_Casting
    template <typename T>
    bool CanCast() const {
        return _CanCast(GetTypeid(), typeid(T));
    }

    /// Return if \c this can be cast to \a type.
    ///
    /// \sa \ref VtValue_Casting
    bool CanCastToTypeOf(VtValue const &other) const {
        return _CanCast(GetTypeid(), other.GetTypeid());
    }

    /// Return if \c this can be cast to \a type.
    ///
    /// \sa \ref VtValue_Casting
    bool CanCastToTypeid(std::type_info const &type) const {
        return _CanCast(GetTypeid(), type);
    }

    /// Returns true iff this value is empty.
    bool IsEmpty() const { return _info.GetLiteral() == 0; }

    /// Return true if the held object provides a hash implementation.
    VT_API bool CanHash() const;

    /// Return a hash code for the held object by calling VtHashValue() on it.
    VT_API size_t GetHash() const;

    friend inline size_t hash_value(VtValue const &val) {
        return val.GetHash();
    }

    /// Tests for equality.
    template <typename T>
    friend bool operator == (VtValue const &lhs, T const &rhs) {
        typedef typename Vt_ValueGetStored<T>::Type Stored;
        return lhs.IsHolding<Stored>() && lhs.UncheckedGet<Stored>() == rhs;
    }
    template <typename T>
    friend bool operator == (T const &lhs, VtValue const &rhs) {
        return rhs == lhs;
    }

    /// Tests for inequality.
    template <typename T>
    friend bool operator != (VtValue const &lhs, T const &rhs) {
        return !(lhs == rhs);
    }
    template <typename T>
    friend bool operator != (T const &lhs, VtValue const &rhs) {
        return !(lhs == rhs);
    }

    /// Test two values for equality.
    bool operator == (const VtValue &rhs) const {
        bool empty = IsEmpty(), rhsEmpty = rhs.IsEmpty();
        if (empty || rhsEmpty)
            return empty == rhsEmpty;
        if (_info.GetLiteral() == rhs._info.GetLiteral())
            return _info.Get()->Equal(_storage, rhs._storage);
        return _EqualityImpl(rhs);
    }
    bool operator != (const VtValue &rhs) const { return !(*this == rhs); }

    /// Calls through to operator << on the held object.
    VT_API friend std::ostream &
    operator << (std::ostream &out, const VtValue &self);

private:
    VT_API const Vt_ShapeData* _GetShapeData() const;
    VT_API size_t _GetNumElements() const;
    friend struct Vt_ValueShapeDataAccess;

    static inline void _Copy(VtValue const &src, VtValue &dst) {
        if (src.IsEmpty()) {
            dst._Clear();
            return;
        }

        _HoldAside tmp(&dst);
        dst._info = src._info;
        if (src._IsLocalAndTriviallyCopyable()) {
            dst._storage = src._storage;
        } else {
            dst._info->CopyInit(src._storage, dst._storage);
        }
    }

    static inline void _Move(VtValue &src, VtValue &dst) noexcept {
        if (src.IsEmpty()) {
            dst._Clear();
            return;
        }

        _HoldAside tmp(&dst);
        dst._info = src._info;
        if (src._IsLocalAndTriviallyCopyable()) {
            dst._storage = src._storage;
        } else {
            dst._info->Move(src._storage, dst._storage);
        }

        src._info.Set(nullptr, 0);
    }

    VtValue const *_ResolveProxy() const {
        return ARCH_UNLIKELY(_IsProxy()) ?
            _info->GetProxiedValue(_storage) : this;
    }

    template <class T>
    inline bool _TypeIs() const {
        std::type_info const &t = typeid(T);
        bool cmp = TfSafeTypeCompare(_info->typeInfo, t);
        return ARCH_UNLIKELY(_IsProxy() && !cmp) ? _TypeIsImpl(t) : cmp;
    }

    VT_API bool _TypeIsImpl(std::type_info const &queriedType) const;

    VT_API bool _EqualityImpl(VtValue const &rhs) const;

    template <class Proxy>
    typename boost::enable_if<VtIsValueProxy<Proxy>, Proxy &>::type
    _GetMutable() {
        typedef typename _TypeInfoFor<Proxy>::Type TypeInfo;
        return TypeInfo::GetMutableObj(_storage);
    }

    template <class T>
    typename boost::disable_if<VtIsValueProxy<T>, T &>::type
    _GetMutable() {
        // If we are a proxy, collapse it out to the real value first.
        if (ARCH_UNLIKELY(_IsProxy()))
            *this = _info->GetProxiedValue(_storage);
        typedef typename _TypeInfoFor<T>::Type TypeInfo;
        return TypeInfo::GetMutableObj(_storage);
    }

    template <class Proxy>
    typename boost::enable_if<VtIsValueProxy<Proxy>, Proxy const &>::type
    _Get() const {
        typedef typename _TypeInfoFor<Proxy>::Type TypeInfo;
        return TypeInfo::GetObj(_storage);
    }

    template <class T>
    typename boost::disable_if<VtIsValueProxy<T>, T const &>::type
    _Get() const {
        typedef typename _TypeInfoFor<T>::Type TypeInfo;
        return TypeInfo::GetObj(_ResolveProxy()->_storage);
    }

    // Helper invoked in case Get fails.  Reports an error and returns a default
    // value for \a queryType.
    VT_API void const *
    _FailGet(Vt_DefaultValueHolder (*factory)(),
             std::type_info const &queryType) const;

    inline void _Clear() {
        // optimize for local types not to deref _info.
        if (_info.GetLiteral() && !_IsLocalAndTriviallyCopyable())
            _info.Get()->Destroy(_storage);
        _info.Set(nullptr, 0);
    }

    inline bool _IsLocalAndTriviallyCopyable() const {
        unsigned int bits = _info.BitsAs<unsigned int>();
        return (bits & (_LocalFlag | _TrivialCopyFlag)) ==
            (_LocalFlag | _TrivialCopyFlag);
    }

    inline bool _IsProxy() const {
        return _info.BitsAs<unsigned int>() & _ProxyFlag;
    }

    VT_API static void _RegisterCast(std::type_info const &from,
                              std::type_info const &to,
                              VtValue (*castFn)(VtValue const &));

    VT_API static VtValue
    _PerformCast(std::type_info const &to, VtValue const &val);

    VT_API static bool
    _CanCast(std::type_info const &from, std::type_info const &to);

    // helper template function for simple casts from From to To.
    template <typename From, typename To>
    static VtValue _SimpleCast(VtValue const &val) {
        return VtValue(To(val.UncheckedGet<From>()));
    }

#ifdef PXR_PYTHON_SUPPORT_ENABLED
    // This grants friend access to a function in the wrapper file for this
    // class.  This lets the wrapper reach down into a value to get a
    // boost::python wrapped object corresponding to the held type.  This
    // facility is necessary to get the python API we want.
    friend TfPyObjWrapper
    Vt_GetPythonObjectFromHeldValue(VtValue const &self);

    VT_API TfPyObjWrapper _GetPythonObject() const;
#endif // PXR_PYTHON_SUPPORT_ENABLED

    _Storage _storage;
    TfPointerAndBits<const _TypeInfo> _info;
};

#if !defined(doxygen)

/// Make a default value.  VtValue uses this to create values to be returned
/// from failed calls to \a Get.  Clients may specialize this for their own
/// types.
template <class T>
struct Vt_DefaultValueFactory {
    /// This function *must* return an object of type \a T.
    static Vt_DefaultValueHolder Invoke() {
        return Vt_DefaultValueHolder::Create<T>();
    }
};

struct Vt_ValueShapeDataAccess {
    static const Vt_ShapeData* _GetShapeData(const VtValue& value) {
        return value._GetShapeData();
    }

    static size_t _GetNumElements(const VtValue& value) {
        return value._GetNumElements();
    }
};

// For performance reasons, the default constructors for vectors,
// matrices, and quaternions do *not* initialize the data of the
// object.  This greatly improves the performance of creating large
// arrays of objects.  However, boost::value_initialized<T>() no
// longer fills the memory of the object with 0 bytes before invoking
// the constructor so we started getting errors complaining about
// uninitialized values.  So, we now use VtZero to construct zeroed
// out vectors, matrices, and quaternions by explicitly instantiating
// the factory for these types. 
//
#define _VT_DECLARE_ZERO_VALUE_FACTORY(r, unused, elem)                 \
VT_API_TEMPLATE_STRUCT(Vt_DefaultValueFactory<VT_TYPE(elem)>);

BOOST_PP_SEQ_FOR_EACH(_VT_DECLARE_ZERO_VALUE_FACTORY,
                      unused,
                      VT_VEC_VALUE_TYPES
                      VT_MATRIX_VALUE_TYPES
                      VT_QUATERNION_VALUE_TYPES)

#undef _VT_DECLARE_ZERO_VALUE_FACTORY

//
// The Get()/IsHolding routines needs to be special-cased to handle getting a
// VtValue *as* a VtValue.
//

template <>
inline const VtValue&
VtValue::Get<VtValue>() const {
    return *this;
}

template <>
inline const VtValue&
VtValue::UncheckedGet<VtValue>() const {
    return *this;
}

template <>
inline bool
VtValue::IsHolding<VtValue>() const {
    return true;
}

// Specialize VtValue::IsHolding<void>() to always return false.
template <>
inline bool
VtValue::IsHolding<void>() const {
    return false;
}

#endif // !doxygen

PXR_NAMESPACE_CLOSE_SCOPE

#endif // VT_VALUE_H
