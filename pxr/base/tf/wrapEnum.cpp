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
#include <boost/python/class.hpp>
#include "pxr/base/tf/pyEnum.h"

using namespace boost::python;

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

static size_t __hash__(Tf_PyEnumWrapper const &self)
{
    return TfHash()(self.value);
}

static boost::python::object _GetValueFromFullName(const std::string &fullName) 
{
    bool found = false;
    const TfEnum value = TfEnum::GetValueFromFullName(fullName, &found);
    if (found) {
        return boost::python::object(value);
    }
    return boost::python::object();
}

} // anonymous namespace 

void wrapEnum()
{
    class_<Tf_PyEnum>("Enum", no_init)
        .def("GetValueFromFullName", _GetValueFromFullName)
        .staticmethod("GetValueFromFullName")
        ;

    class_<Tf_PyEnumWrapper, bases<Tf_PyEnum> >
        ("Tf_PyEnumWrapper", no_init)
        .add_property("value", &Tf_PyEnumWrapper::GetValue)
        .add_property("name", &Tf_PyEnumWrapper::GetName)
        .add_property("fullName", &Tf_PyEnumWrapper::GetFullName)
        .add_property("displayName", &Tf_PyEnumWrapper::GetDisplayName)
        .def("allEnumerators", &Tf_PyEnumWrapper::GetAllEnumerators)
        .def("__int__", &Tf_PyEnumWrapper::GetValue)
        .def("__str__", &Tf_PyEnumWrapper::GetDisplayName)
        .def("__repr__", Tf_PyEnumRepr)
        .def("__hash__", __hash__)
        .def(self == long())
        .def(self == self)
        .def(self < self)
        .def(self <= self)
        .def(self > self)
        .def(self >= self)
        .def(long() | self)
        .def(self | long())
        .def(self | self)
        .def(long() & self)
        .def(self & long())
        .def(self & self)
        .def(long() ^ self)
        .def(self ^ long())
        .def(self ^ self)
        .def( ~ self)
        ;
}
