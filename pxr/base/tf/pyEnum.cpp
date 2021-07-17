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

#include "pxr/base/tf/pyEnum.h"

#include "pxr/base/tf/instantiateSingleton.h"
#include "pxr/base/tf/mallocTag.h"
#include "pxr/base/tf/pyWrapContext.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(Tf_PyEnumRegistry);

using std::string;

using namespace boost::python;

Tf_PyEnumRegistry::Tf_PyEnumRegistry()
{
    // Register general conversions to and from python for TfEnum.
    to_python_converter<TfEnum, _EnumToPython<TfEnum> >();

    _EnumFromPython<TfEnum>();
    _EnumFromPython<int>();
    _EnumFromPython<unsigned int>();
    _EnumFromPython<long>();
    _EnumFromPython<unsigned long>();
}

// CODE_COVERAGE_OFF No way to destroy the enum registry currently.
Tf_PyEnumRegistry::~Tf_PyEnumRegistry()
{
    // release our references on all the objects we own.
    TF_FOR_ALL(i, _objectsToEnums)
        decref(i->first);
}
// CODE_COVERAGE_ON

string
Tf_PyEnumRepr(object const &self) {
    string moduleName = extract<string>(self.attr("__module__"));
    string baseName = extract<string>(self.attr("_baseName"));
    string name = extract<string>(self.attr("name"));

    return TfStringGetSuffix(moduleName) + "." +
        (baseName.empty() ? string() : baseName + ".") +
        name;
}

string Tf_PyCleanEnumName(string name)
{
    string pkgName =
        Tf_PyWrapContextManager::GetInstance().GetCurrentContext();
    if (TfStringStartsWith(name, pkgName) && name != pkgName) {
        name.erase(0, pkgName.size());
    }
    return TfStringReplace(name, " ", "_");
}

void Tf_PyEnumAddAttribute(boost::python::scope &s,
                           const std::string &name,
                           const boost::python::object &value) {
    // Skip exporting attr if the scope already has an attribute
    // with that name, but do make sure to place it in .allValues
    // for the class.
    if (PyObject_HasAttrString(s.ptr(), name.c_str())) {
        TF_CODING_ERROR(
            "Ignoring enum value '%s'; an attribute with that "
            "name already exists in that scope.", name.c_str());
    }
    else {
        s.attr(name.c_str()) = value;
    }
}

boost::python::list
Tf_PyEnumWrapper::GetAllEnumerators(bool cleanNames) const {
    boost::python::list valueList;
    const auto& ti = value.GetType();
    for (auto name : TfEnum::GetAllNames(value)) {
        bool foundIt;
        TfEnum eVal = TfEnum::GetValueFromName(ti, name, &foundIt);
        if (!foundIt)
            continue;
        if (cleanNames)
            name = Tf_PyCleanEnumName(name);

        // convert value to python.
        Tf_PyEnumWrapper wrappedValue(std::move(name), eVal);
        boost::python::object pyVal(wrappedValue);
        pyVal.attr("_baseName") = std::string();
        valueList.append(std::move(pyVal));
    }
    return valueList;
}

void
Tf_PyEnumRegistry::
RegisterValue(TfEnum const &e, object const &obj)
{
    TfAutoMallocTag2 tag("Tf", "Tf_PyEnumRegistry::RegisterValue");
    
    // we take a reference to obj.
    incref(obj.ptr());

    _enumsToObjects[e] = obj.ptr();
    _objectsToEnums[obj.ptr()] = e;
}

PXR_NAMESPACE_CLOSE_SCOPE
