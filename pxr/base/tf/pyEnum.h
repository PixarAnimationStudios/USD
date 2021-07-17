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
#ifndef PXR_BASE_TF_PY_ENUM_H
#define PXR_BASE_TF_PY_ENUM_H

/// \file tf/pyEnum.h
/// Provide facilities for wrapping enums for script.

#include "pxr/pxr.h"

#include "pxr/base/tf/api.h"
#include "pxr/base/tf/pyObjWrapper.h"
#include "pxr/base/tf/pyUtils.h"
#include "pxr/base/tf/type.h"

#include "pxr/base/arch/demangle.h"
#include "pxr/base/tf/enum.h"
#include "pxr/base/tf/hash.h"
#include "pxr/base/tf/hashmap.h"
#include "pxr/base/tf/iterator.h"
#include "pxr/base/tf/singleton.h"
#include "pxr/base/tf/stringUtils.h"

#include <boost/python/class.hpp>
#include <boost/python/converter/from_python.hpp>
#include <boost/python/converter/registered.hpp>
#include <boost/python/converter/rvalue_from_python_data.hpp>
#include <boost/python/list.hpp>
#include <boost/python/object.hpp>
#include <boost/python/operators.hpp>
#include <boost/python/refcount.hpp>
#include <boost/python/scope.hpp>
#include <boost/python/to_python_converter.hpp>
#include <boost/python/tuple.hpp>

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

/// \class Tf_PyEnum
///
/// Base class of all python enum classes.
class Tf_PyEnum { };

/// \class Tf_PyEnumRegistry
///
/// This is a private class that manages registered enum objects.
/// \private
class Tf_PyEnumRegistry {

  public:
    typedef Tf_PyEnumRegistry This;
    
  private:
    Tf_PyEnumRegistry();
    virtual ~Tf_PyEnumRegistry();
    friend class TfSingleton<This>;
    
  public:

    TF_API static This &GetInstance() {
        return TfSingleton<This>::GetInstance();
    }
    
    TF_API
    void RegisterValue(TfEnum const &e, boost::python::object const &obj);

    template <typename T>
    void RegisterEnumConversions() {
        // Register conversions to and from python.
        boost::python::to_python_converter<T, _EnumToPython<T> >();
        _EnumFromPython<T>();
    }

  private:

    template <typename T>
    struct _EnumFromPython {
        _EnumFromPython() {
            boost::python::converter::registry::insert
                (&convertible, &construct, boost::python::type_id<T>());
        }
        static void *convertible(PyObject *obj) {
            TfHashMap<PyObject *, TfEnum, _ObjectHash> const &o2e =
                Tf_PyEnumRegistry::GetInstance()._objectsToEnums;
            TfHashMap<PyObject *, TfEnum, _ObjectHash>::const_iterator
                i = o2e.find(obj);
            // In the case of producing a TfEnum or an integer, any
            // registered enum type is fine.  In all other cases, the
            // enum types must match.
            if (boost::is_same<T, TfEnum>::value ||
                (boost::is_integral<T>::value && !boost::is_enum<T>::value))
                return i != o2e.end() ? obj : 0;
            else
                return (i != o2e.end() && i->second.IsA<T>()) ? obj : 0;
        }
        static void construct(PyObject *src, boost::python::converter::
                              rvalue_from_python_stage1_data *data) {
            void *storage =
                ((boost::python::converter::
                  rvalue_from_python_storage<T> *)data)->storage.bytes;
            new (storage) T(_GetEnumValue(src, (T *)0));
            data->convertible = storage;
        }
    private:
        // Overloads to explicitly allow conversion of the TfEnum integer
        // value to other enum/integral types.
        template <typename U>
        static U _GetEnumValue(PyObject *src, U *) {
            return U(Tf_PyEnumRegistry::GetInstance()._objectsToEnums[src].
                GetValueAsInt());
        }
        static TfEnum _GetEnumValue(PyObject *src, TfEnum *) {
            return Tf_PyEnumRegistry::GetInstance()._objectsToEnums[src];
        }
    };
    
    template <typename T>
    struct _EnumToPython {
        static PyObject *convert(T const &t);
    };

    // Since our enum objects live as long as the registry does, we can use the
    // pointer values for a hash.
    struct _ObjectHash {
        size_t operator()(PyObject *o) const {
            return reinterpret_cast<size_t>(o);
        }
    };
    
    TfHashMap<TfEnum, PyObject *, TfHash> _enumsToObjects;
    TfHashMap<PyObject *, TfEnum, _ObjectHash> _objectsToEnums;

};

TF_API_TEMPLATE_CLASS(TfSingleton<Tf_PyEnumRegistry>);

// Private function used for __repr__ of wrapped enum types.
TF_API
std::string Tf_PyEnumRepr(boost::python::object const &self);

// Private base class for types which are instantiated and exposed to python
// for each registered enum type.
struct Tf_PyEnumWrapper
    : public Tf_PyEnum, boost::totally_ordered<Tf_PyEnumWrapper>
{
    typedef Tf_PyEnumWrapper This;

    Tf_PyEnumWrapper(std::string const &n, TfEnum const &val) :
        name(n), value(val) {}
    long GetValue() const {
        return value.GetValueAsInt();
    }
    std::string GetName() const{
        return name;
    }
    std::string GetDisplayName() const {
        return TfEnum::GetDisplayName(value);
    }
    std::string GetFullName() const {
        return TfEnum::GetFullName(value);
    }
    boost::python::list GetAllEnumerators(bool cleanNames = true) const;

    friend bool operator ==(Tf_PyEnumWrapper const &self,
                            long other) {
        return self.value.GetValueAsInt() == other;
    }

    friend bool operator ==(Tf_PyEnumWrapper const &lhs,
                            Tf_PyEnumWrapper const &rhs) {
        return lhs.value == rhs.value;
    }

    friend bool operator <(Tf_PyEnumWrapper const &lhs,
                           Tf_PyEnumWrapper const &rhs)
    {
        // If same, not less.
        if (lhs == rhs)
            return false;
        // If types don't match, string compare names.
        if (!lhs.value.IsA(rhs.value.GetType()))
            return TfEnum::GetFullName(lhs.value) <
                TfEnum::GetFullName(rhs.value);
        // If types do match, numerically compare values.
        return lhs.GetValue() < rhs.GetValue();
    }
    
    //
    // XXX Bitwise operators for Enums are a temporary measure to support the
    // use of Enums as Bitmasks in libSd.  It should be noted that Enums are
    // NOT closed under these operators. The proper place for such operators
    // is in a yet-nonexistent Bitmask type.  
    //

    friend TfEnum operator |(Tf_PyEnumWrapper const &lhs,
                        Tf_PyEnumWrapper const &rhs) {
        if (lhs.value.IsA(rhs.value.GetType())) {
            return TfEnum(lhs.value.GetType(),
                          lhs.value.GetValueAsInt() |
                          rhs.value.GetValueAsInt());
        }
        TfPyThrowTypeError("Enum type mismatch");
        return TfEnum();
    }
    friend TfEnum operator |(Tf_PyEnumWrapper const &lhs, long rhs) {
        return TfEnum(lhs.value.GetType(), lhs.value.GetValueAsInt() | rhs);
    }
    friend TfEnum operator |(long lhs, Tf_PyEnumWrapper const &rhs) {
        return TfEnum(rhs.value.GetType(), lhs | rhs.value.GetValueAsInt());
    }
    
    friend TfEnum operator &(Tf_PyEnumWrapper const &lhs,
                             Tf_PyEnumWrapper const &rhs) {
        if (lhs.value.IsA(rhs.value.GetType())) {
            return TfEnum(lhs.value.GetType(),
                          lhs.value.GetValueAsInt() &
                          rhs.value.GetValueAsInt());
        }
        TfPyThrowTypeError("Enum type mismatch");
        return TfEnum();
    }
    friend TfEnum operator &(Tf_PyEnumWrapper const &lhs, long rhs) {
        return TfEnum(lhs.value.GetType(), lhs.value.GetValueAsInt() & rhs);
    }
    friend TfEnum operator &(long lhs, Tf_PyEnumWrapper const &rhs) {
        return TfEnum(rhs.value.GetType(), lhs & rhs.value.GetValueAsInt());
    }
    
    friend TfEnum operator ^(Tf_PyEnumWrapper const &lhs,
                             Tf_PyEnumWrapper const &rhs) {
        if (lhs.value.IsA(rhs.value.GetType())) {
            return TfEnum(lhs.value.GetType(),
                          lhs.value.GetValueAsInt() ^
                          rhs.value.GetValueAsInt());
        }
        TfPyThrowTypeError("Enum type mismatch");
        return TfEnum();
    }
    friend TfEnum operator ^(Tf_PyEnumWrapper const &lhs, long rhs) {
        return TfEnum(lhs.value.GetType(), lhs.value.GetValueAsInt() ^ rhs);
    }
    friend TfEnum operator ^(long lhs, Tf_PyEnumWrapper const &rhs) {
        return TfEnum(rhs.value.GetType(), lhs ^ rhs.value.GetValueAsInt());
    }
    
    friend TfEnum operator ~(Tf_PyEnumWrapper const &rhs) {
        return TfEnum(rhs.value.GetType(), ~rhs.value.GetValueAsInt());
    }
    std::string name;
    TfEnum value;
};

template <typename T>
PyObject *
Tf_PyEnumRegistry::_EnumToPython<T>::convert(T const &t)
{
    TfEnum e(t);

    // If there is no registered enum object, create a new one of
    // the appropriate type.
    if (!Tf_PyEnumRegistry::GetInstance()._enumsToObjects.count(e)) {
        std::string name = ArchGetDemangled(e.GetType());
        name = TfStringReplace(name, " ", "_");
        name = TfStringReplace(name, "::", "_");
        name = TfStringReplace(name, "<", "_");
        name = TfStringReplace(name, ">", "_");
        name = "AutoGenerated_" + name + "_" +
            TfStringify(e.GetValueAsInt());

        boost::python::object wrappedVal =
            boost::python::object(Tf_PyEnumWrapper(name, e));

        wrappedVal.attr("_baseName") = std::string();

        Tf_PyEnumRegistry::GetInstance().RegisterValue(e, wrappedVal);
    }
    
    return boost::python::
        incref(Tf_PyEnumRegistry::GetInstance()._enumsToObjects[e]);
}

// Private template class which is instantiated and exposed to python for each
// registered enum type.
template <typename T>
struct Tf_TypedPyEnumWrapper : Tf_PyEnumWrapper
{
    Tf_TypedPyEnumWrapper(std::string const &n, TfEnum const &val) :
        Tf_PyEnumWrapper(n, val) {}

    static boost::python::object GetValueFromName(const std::string& name) {
        bool found = false;
        const TfEnum value = TfEnum::GetValueFromName<T>(name, &found);
        if (found) return boost::python::object(value);
        return boost::python::object();
    }
};

// Removes the MFB package prefix from name if it starts with it, and replaces
// spaces with underscores.
TF_API
std::string Tf_PyCleanEnumName(std::string name);

// Adds attribute of given name with given value to given scope.
// Issues a coding error if attribute by that name already existed.
TF_API
void Tf_PyEnumAddAttribute(boost::python::scope &s,
                           const std::string &name,
                           const boost::python::object &value);

/// \class TfPyWrapEnum
///
/// Used to wrap enum types for script.
///
/// TfPyWrapEnum provides a way to wrap enums for python, tying in with the \a
/// TfEnum system, and potentially providing automatic wrapping by using names
/// registered with the \a TfEnum system and by making some assumptions about
/// the way we structure our code.  Enums that are not registered with TfEnum
/// may be manually wrapped using boost::python::enum_ instead.
///
/// Example usage.  For an enum that looks like this:
/// \code
/// enum FooChoices {
///    FooFirst,
///    FooSecond,
///    FooThird
/// };
/// \endcode
///
/// Which has been registered in the \a TfEnum system and has names provided for
/// all values, it may be wrapped like this:
/// \code
/// TfPyWrapEnum<FooChoices>();
/// \endcode
///
/// The enum will appear in script as Foo.Choices.{First, Second, Third} and
/// the values will also appear as Foo.{First, Second, Third}.
///
/// An enum may be given an explicit name by passing a string to
/// TfPyWrapEnum's constructor.
///
/// If the enum is a C++11 scoped enum (aka enum class), the values will appear
/// as Foo.Choices.{First, Second, Third} in the following example:
/// \code
/// enum class FooChoices {
///    First,
///    Second,
///    Third
/// };
/// \endcode
///

// Detect scoped enums by using that the C++ standard does not allow them to
// be converted to int implicitly.
template <typename T, bool IsScopedEnum = !std::is_convertible<T, int>::value>
struct TfPyWrapEnum {

private:
    typedef boost::python::class_<
        Tf_TypedPyEnumWrapper<T>, boost::python::bases<Tf_PyEnumWrapper> >
    _EnumPyClassType;

public:

    /// Construct an enum wrapper object.
    /// If \a name is provided, it is used as the name of the enum.  Otherwise
    /// the type name of \a T is used, with a leading MFB package name
    /// stripped.
    explicit TfPyWrapEnum( std::string const &name = std::string())
    {
        using namespace boost::python;

        const bool explicitName = !name.empty();

        // First, take either the given name, or the demangled type name.
        std::string enumName = explicitName ? name :
            TfStringReplace(ArchGetDemangled(typeid(T)), "::", ".");

        // If the name is dotted, take everything before the dot as the base
        // name.  This is used in repr.
        std::string baseName = TfStringGetBeforeSuffix(enumName);
        if (baseName == enumName)
            baseName = std::string();

        // If the name is dotted, take the last element as the enum name.
        if (!TfStringGetSuffix(enumName).empty())
            enumName = TfStringGetSuffix(enumName);

        // If the name was not explicitly given, then clean it up by removing
        // the mfb package name prefix if it exists.
        if (!explicitName) {
            if (!baseName.empty())
                baseName = Tf_PyCleanEnumName(baseName);
            else
                enumName = Tf_PyCleanEnumName(enumName);
        }
        
        if (IsScopedEnum) {
            // Make the enumName appear in python representation
            // for scoped enums.
            if (!baseName.empty()) {
                baseName += ".";
            }
            baseName += enumName;
        }

        // Make a python type for T.
        _EnumPyClassType enumClass(enumName.c_str(), no_init);
        enumClass.def("GetValueFromName", &Tf_TypedPyEnumWrapper<T>::GetValueFromName, arg("name"));
        enumClass.staticmethod("GetValueFromName");
        enumClass.setattr("_baseName", baseName);

        // Register conversions for it.
        Tf_PyEnumRegistry::GetInstance().RegisterEnumConversions<T>();

        // Export values.  Only clean names if basename is empty (i.e. the enum
        // is top-level).
        _ExportValues(baseName.empty(), enumClass);

        // Register with Tf so that python clients of a TfType
        // that represents an enum are able to get to the equivalent 
        // python class with .pythonclass
        const TfType &type = TfType::Find<T>();
        if (!type.IsUnknown())
            type.DefinePythonClass(enumClass);
    }
    
  private:

    /// Export all values in this enum to the enclosing scope.
    /// If no explicit names have been registered, this will export the TfEnum
    /// registered names and values (if any).
    void _ExportValues(bool cleanNames, _EnumPyClassType &enumClass) {
        boost::python::list valueList;

        std::vector<std::string> names = TfEnum::GetAllNames<T>();
        TF_FOR_ALL(name, names) {
            bool success = false;
            TfEnum enumValue = TfEnum::GetValueFromName<T>(*name, &success);
            if (!success) {
                continue;
            }

            std::string cleanedName = cleanNames ?
                Tf_PyCleanEnumName(*name) : *name;

            // convert value to python.
            Tf_TypedPyEnumWrapper<T> wrappedValue(cleanedName, enumValue);
            boost::python::object pyValue(wrappedValue);

            // register it as the python object for this value.
            Tf_PyEnumRegistry::GetInstance().RegisterValue(enumValue, pyValue);

            // Take all the values and export them into the current scope.
            std::string valueName = wrappedValue.GetName();
            if (IsScopedEnum) {
                // If scoped enum, enum values appear on the enumClass ...
                boost::python::scope s(enumClass);
                Tf_PyEnumAddAttribute(s, valueName, pyValue);
            } else {
                // ... otherwise, enum values appear on the enclosing scope.
                boost::python::scope s;
                Tf_PyEnumAddAttribute(s, valueName, pyValue);
            }

            valueList.append(pyValue);
        }

        // Add a tuple of all the values to the enum class.
        enumClass.setattr("allValues", boost::python::tuple(valueList));
    }

};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_BASE_TF_PY_ENUM_H
