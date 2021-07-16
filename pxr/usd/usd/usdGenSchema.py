#!/pxrpythonsubst
#
# Copyright 2016 Pixar
#
# Licensed under the Apache License, Version 2.0 (the "Apache License")
# with the following modification; you may not use this file except in
# compliance with the Apache License and the following modification to it:
# Section 6. Trademarks. is deleted and replaced with:
#
# 6. Trademarks. This License does not grant permission to use the trade
#    names, trademarks, service marks, or product names of the Licensor
#    and its affiliates, except as required to comply with Section 4(c) of
#    the License and to reproduce the content of the NOTICE file.
#
# You may obtain a copy of the Apache License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the Apache License with the above modification is
# distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the Apache License for the specific
# language governing permissions and limitations under the Apache License.
#
"""
This script generates C++ classes and supporting Python code for USD Schemata.
It is driven by a USD layer (schema.usda) that defines the schema classes.

This USD layer must meet the following preconditions in order for code to be
generated that will compile and work with USD Core successfully:
    
    * Must specify the libraryName as layer metadata.
    * Schema typenames must be unique across all libraries.
    * Attribute names and tokens must be camelCased valid identifiers.
    * usd/schema.usda must exist in the LayerStack, not necessarily as a 
        directly subLayer.
"""

from __future__ import print_function
import sys, os, re, inspect
import keyword
from argparse import ArgumentParser
from collections import namedtuple

from jinja2 import Environment, FileSystemLoader
from jinja2.exceptions import TemplateNotFound, TemplateSyntaxError

from pxr import Plug, Sdf, Usd, Vt, Tf

# Object used for printing. This gives us a way to control output with regards 
# to program arguments such as --quiet.
class _Printer():
    def __init__(self, quiet=False):
        self._quiet = quiet

    def __PrintImpl(self, stream, *args):
        if len(args):
            print(*args, file=stream)
        else:
            print(file=stream)

    def __call__(self, *args):
        if not self._quiet:
            self.__PrintImpl(sys.stdout, *args)

    def Err(self, *args):
        self.__PrintImpl(sys.stderr, *args)

    def SetQuiet(self, quiet):
        self._quiet = quiet

Print = _Printer()

#------------------------------------------------------------------------------#
# Tokens                                                                       #
#------------------------------------------------------------------------------#

# Name of script, e.g. "usdGenSchema"
PROGRAM_NAME = os.path.splitext(os.path.basename(sys.argv[0]))[0]

# Custom-data key authored on an API schema class prim in the schema definition,
# to define the type of API schema.
API_SCHEMA_TYPE = "apiSchemaType"

# Possible values for customData["apiSchemaType"] on an API schema class prim.
NON_APPLIED = "nonApplied"
SINGLE_APPLY = "singleApply"
MULTIPLE_APPLY = "multipleApply"
API_SCHEMA_TYPE_TOKENS = [NON_APPLIED, SINGLE_APPLY, MULTIPLE_APPLY]

# Custom-data key authored on a multiple-apply API schema class prim in the 
# schema definition, to define prefix for properties created by the API schema. 
PROPERTY_NAMESPACE_PREFIX = "propertyNamespacePrefix"

# Custom-data key optionally authored on a single-apply API schema class prim 
# in the schema definition to define a list of typed schemas that the API 
# schema will be automatically applied to in the schema registry. 
API_AUTO_APPLY = "apiSchemaAutoApplyTo"
API_CAN_ONLY_APPLY = "apiSchemaCanOnlyApplyTo"
API_ALLOWED_INSTANCE_NAMES = "apiSchemaAllowedInstanceNames"
API_SCHEMA_INSTANCES = "apiSchemaInstances"

# Custom-data key authored on a concrete typed schema class prim in the schema
# definition, to define fallbacks for the type that can be saved in root layer
# metadata to provide fallback types to versions of Usd without the schema 
# class type.
FALLBACK_TYPES = "fallbackTypes"

#------------------------------------------------------------------------------#
# Parsed Objects                                                               #
#------------------------------------------------------------------------------#

def _SanitizeDoc(doc, leader):
    """Cleanup the doc string in several ways:
      * Convert None to empty string
      * Replace new line chars with doxygen comments
      * Strip leading white space per line
    """
    if doc is None:
        return ''
    
    return leader.join([line.lstrip() for line in doc.split('\n')])


def _ListOpToList(listOp):
    """Apply listOp to an empty list, yielding a list."""
    return listOp.ApplyOperations([]) if listOp else []

def _GetDefiningLayerAndPrim(stage, schemaName):
    """ Searches the stage LayerStack for a prim whose name is equal to
        schemaName.
    """
    # SchemaBase is not actually defined in the core schema file, but this
    # behavior causes the code generator to produce correct C++ inheritance.
    if schemaName == 'SchemaBase':
        return (stage.GetLayerStack()[-1], None)
    
    else:
        for layer in stage.GetLayerStack():
            for sdfPrim in layer.rootPrims:
                if sdfPrim.name == schemaName:
                    return (layer, sdfPrim)

    raise Exception("Could not find the defining layer for schema: %s" % schemaName)
                    

def _GetLibMetadata(layer):
    """ Return a dictionary of library-specific data found in layer."""
    
    globalPrim = layer.GetPrimAtPath('/GLOBAL')
    if not globalPrim:
        raise Exception("Code generation requires a \"/GLOBAL\" prim with "
            "customData to define at least libraryName. GLOBAL prim not found.")
    
    if not globalPrim.customData:
        raise Exception("customData is either empty or not defined on /GLOBAL "
            "prim. At least \"libraryName\" entries in customData are required "
            "for code generation.")
    
    # Return a copy of customData to avoid accessing an invalid map proxy during
    # template rendering.
    return dict(globalPrim.customData)


def _GetLibName(layer):
    """ Return the libraryName defined in layer."""
    
    libData = _GetLibMetadata(layer)
    if 'libraryName' not in libData:
        raise Exception("Code generation requires that \"libraryName\" be defined "
            "in customData on /GLOBAL prim.")

    return libData['libraryName']
    

def _GetLibPath(layer):
    """ Return the libraryPath defined in layer."""

    if _SkipCodeGenForLayer(layer):
        return ""

    libData = _GetLibMetadata(layer)
    if 'libraryPath' not in libData: 
        raise Exception("Code generation requires that \"libraryPath\" be "
            "defined in customData on /GLOBAL prim or the schema must be "
            "declared codeless, by specifying skipCodeGeneration=true. "
            "The format for libraryPath is \"path/to/lib\".")

    return libData['libraryPath']


def _GetLibPrefix(layer):
    """ Return the libraryPrefix defined in layer. If not defined, fall
    back to ProperCase(libraryName). Used to prefix all class names"""
    
    return _GetLibMetadata(layer).get('libraryPrefix', 
        _ProperCase(_GetLibName(layer)))


def _GetLibTokens(layer):
    """ Return dictionary of library-wide tokens defined in layer. """
    return _GetLibMetadata(layer).get('libraryTokens', {})
    

def _GetTokensPrefix(layer):
    """ Return the tokensPrefix defined in layer."""

    return _GetLibMetadata(layer).get('tokensPrefix', 
        _GetLibPrefix(layer))


def _GetUseExportAPI(layer):
    """ Return the useExportAPI defined in layer."""

    return _GetLibMetadata(layer).get('useExportAPI', True)

def _SkipCodeGenForLayer(layer):
    """ Return whether the layer specifies that code generation should 
    be skipped for its schemas."""

    # This can be called on sublayers which may not necessarily have lib 
    # metadata so we can ignore exceptions and return false.
    try:
        return _GetLibMetadata(layer).get('skipCodeGeneration', False)
    except:
        return False

def _SkipCodeGenForSchemaLib(stage):
    """ Return whether the stage has a layer that specifies that code generation
    should be skipped for its schemas and therefore for the the entire schema
    library."""

    for layer in stage.GetLayerStack():
        if _SkipCodeGenForLayer(layer):
            return True
    return False
    
def _UpperCase(aString):
    return aString.upper()


def _LowerCase(aString):
    return aString.lower()


def _ProperCase(aString):
    """Returns the given string (camelCase or ProperCase) in ProperCase,
        stripping out any non-alphanumeric characters.
    """
    if len(aString) > 1:
        return ''.join([s[0].upper() + s[1:] for s in re.split(r'\W+', aString) \
            if len(s) > 0])
    else:
        return aString.upper()


def _CamelCase(aString):
    """Returns the given string (camelCase or ProperCase) in camelCase,
        stripping out any non-alphanumeric characters.
    """
    if len(aString) > 1:
        pcase = _ProperCase(aString)
        return pcase[0].lower() + pcase[1:]
    else:
        return aString.lower()

    
Token = namedtuple('Token', ['id', 'value', 'desc'])

class PropInfo(object):
    class CodeGen:
        """Specifies how code gen constructs get methods for a property
        
        - generated: Auto generate the full Public API
        - custom: Generate the header only. User responsible for implementation.
        
        See documentation on Generating Schemas for more information.
        """
        Generated = 'generated'
        Custom = 'custom'
        
    def __init__(self, sdfProp):
        # Allow user to specify custom naming through customData metadata.
        self.customData = dict(sdfProp.customData)

        self.name       = _CamelCase(sdfProp.name)
        self.apiName    = self.customData.get('apiName', self.name)
        self.apiGet     = self.customData.get('apiGetImplementation', self.CodeGen.Generated)
        if self.apiGet not in [self.CodeGen.Generated, self.CodeGen.Custom]:
            Print.Err("Token '%s' is not valid." % self.apiGet)
        self.rawName    = sdfProp.name
        self.doc        = _SanitizeDoc(sdfProp.documentation, '\n    /// ')
        self.custom     = sdfProp.custom

class RelInfo(PropInfo):
    def __init__(self, sdfProp):
        super(RelInfo, self).__init__(sdfProp)

# Map an Sdf.ValueTypeName.XXX object to the 'XXX' token string -- we use this
# to go from sdf attribute types to their symbolic tokens, for example:
# Sdf.ValueTypeNames.UInt -> 'UInt'.
valueTypeNameToStr = dict(
    [(getattr(Sdf.ValueTypeNames, n), n)
     for n in dir(Sdf.ValueTypeNames)
     if isinstance(getattr(Sdf.ValueTypeNames, n), Sdf.ValueTypeName)])

def _GetSchemaDefException(msg, path):
    errorPrefix = ('Invalid schema definition at ' 
                   + '<' + str(path) + '>')
    errorSuffix = ('See '
                   'https://graphics.pixar.com/usd/docs/api/'
                   '_usd__page__generating_schemas.html '
                   'for more information.\n')
    errorMsg = lambda s: errorPrefix + '\n' + s + '\n' + errorSuffix
    return Exception(errorMsg(msg))

class AttrInfo(PropInfo):
    def __init__(self, sdfProp):
        super(AttrInfo, self).__init__(sdfProp)
        self.allowedTokens = sdfProp.GetInfo('allowedTokens')
        
        self.variability = str(sdfProp.variability).replace('Sdf.', 'Sdf')
        self.fallback = sdfProp.default
        self.typeName = sdfProp.typeName

        if sdfProp.typeName not in valueTypeNameToStr:
            raise _GetSchemaDefException(
                        "Code generation requires that all attributes "
                        "have a known type "
                        "(<%s> has type '%s', which is not a member of "
                        "Sdf.ValueTypeNames.)"
                        % (sdfProp.path, sdfProp.typeName), sdfProp.path)
        else:
            self.usdType = "SdfValueTypeNames->%s" % (
                valueTypeNameToStr[sdfProp.typeName])

        self.details = [
            ('Declaration', '`%s`' % _GetAttrDeclaration(sdfProp)),
            ('C++ Type', self.typeName.cppTypeName),
            ('\\ref Usd_Datatypes "Usd Type"', self.usdType),
        ]

        if self.variability == "SdfVariabilityUniform":
            self.details.append(('\\ref SdfVariability "Variability"', self.variability))

        if self.allowedTokens:
            tokenListStr = ', '.join(
                [x if x else '""' for x in self.allowedTokens])
            self.details.append(('\\ref ' + \
                _GetTokensPrefix(sdfProp.layer) + \
                'Tokens "Allowed Values"', tokenListStr))


def _GetAttrDeclaration(attrSpec):
    anon = Sdf.Layer.CreateAnonymous()
    ps = Sdf.PrimSpec(anon, '_', Sdf.SpecifierDef)
    tmpAttr = Sdf.AttributeSpec(ps, attrSpec.name, attrSpec.typeName, attrSpec.variability)
    tmpAttr.default = attrSpec.default
    return tmpAttr.GetAsText().strip()


def _ExtractNames(sdfPrim, customData):
    usdPrimTypeName = sdfPrim.path.name
    className = customData.get('className', _ProperCase(usdPrimTypeName))
    cppClassName = _GetLibPrefix(sdfPrim.layer) + className
    baseFileName = customData.get('fileName', _CamelCase(className))
    
    return usdPrimTypeName, className, cppClassName, baseFileName


# Determines if a prim 'p' inherits from Typed
def _IsTyped(p):
    def _FindAllInherits(p):
        if p.GetMetadata('inheritPaths'):
            inherits = list(p.GetMetadata('inheritPaths').ApplyOperations([]))
        else:
            inherits = []
        for path in inherits:
            p2 = p.GetStage().GetPrimAtPath(path)
            if p2:
                inherits += _FindAllInherits(p2)
        return inherits

    return Sdf.Path('/Typed') in set(_FindAllInherits(p))

class ClassInfo(object):
    def __init__(self, usdPrim, sdfPrim):
        # First validate proper class naming...
        if (sdfPrim.typeName != sdfPrim.path.name and
            sdfPrim.typeName != ''):
            raise _GetSchemaDefException(
                "Code generation requires that every instantiable "
                "class's name must match its declared type "
                "('%s' and '%s' do not match.)" % 
                (sdfPrim.typeName, sdfPrim.path.name), sdfPrim.path)
        
        # NOTE: usdPrim should ONLY be used for querying information regarding
        # the class's parent in order to avoid duplicating class members during
        # code generation.
        inherits = usdPrim.GetMetadata('inheritPaths') 
        inheritsList = _ListOpToList(inherits)

        # We do not allow multiple inheritance 
        numInherits = len(inheritsList)
        if numInherits > 1:
            raise _GetSchemaDefException(
                'Schemas can only inherit from one other schema '
                'at most. This schema inherits from %d (%s).' 
                 % (numInherits, ', '.join(map(str, inheritsList))), 
                 sdfPrim.path)

        # Allow user to specify custom naming through customData metadata.
        self.customData = dict(sdfPrim.customData)

        # For accumulation of AttrInfo objects
        self.attrs = {}
        self.rels = {}
        self.attrOrder = []
        self.relOrder = []
        self.tokens = set()

        # Important names
        (self.usdPrimTypeName,
         self.className,
         self.cppClassName,
         self.baseFileName) = _ExtractNames(sdfPrim, self.customData)

        self.parentCppClassName = ''

        # We must also hold onto the authored prim name in schema.usda
        # for cases in which we must differentiate that from the authored
        # className in customdata. For example, UsdModelAPI vs UsdGeomModelAPI
        self.primName = sdfPrim.path.name

        # Class Parent's Info
        parentClass = inheritsList[0].name if inheritsList else 'SchemaBase'
        (parentLayer,
         parentPrim) = _GetDefiningLayerAndPrim(usdPrim.GetStage(), parentClass)
        self.parentLibPath = _GetLibPath(parentLayer)
        parentCustomData = {}
        if parentPrim is not None:
            parentCustomData = dict(parentPrim.customData)
            (parentUsdName, parentClassName,
             self.parentCppClassName, self.parentBaseFileName) = \
             _ExtractNames(parentPrim, parentCustomData)
        # Only Typed and APISchemaBase are allowed to have no inherits, since 
        # these are the core base types for all the typed and API schemas 
        # respectively.
        elif self.cppClassName in ["UsdTyped", 'UsdAPISchemaBase']:
            self.parentCppClassName = "UsdSchemaBase"
            self.parentBaseFileName = "schemaBase"

        # Extra Class Metadata
        self.doc = _SanitizeDoc(sdfPrim.documentation, '\n/// ')
        self.typeName = sdfPrim.typeName
        self.extraIncludes = self.customData.get('extraIncludes', None)

        # Do not to inherit the type name of parent classes.
        if inherits:
            for path in inherits.GetAddedOrExplicitItems():
                parentTypeName = parentLayer.GetPrimAtPath(path).typeName
                if parentTypeName == self.typeName:
                    self.typeName = ''

        self.isConcrete = bool(self.typeName)
        self.isTyped = _IsTyped(usdPrim)
        self.isAPISchemaBase = self.cppClassName == 'UsdAPISchemaBase'

        self.fallbackPrimTypes = self.customData.get(FALLBACK_TYPES)

        self.isApi = not self.isTyped and not self.isConcrete and \
                not self.isAPISchemaBase
        self.apiSchemaType = self.customData.get(API_SCHEMA_TYPE, 
                SINGLE_APPLY if self.isApi else None)
        self.propertyNamespacePrefix = \
            self.customData.get(PROPERTY_NAMESPACE_PREFIX)
        self.apiAutoApply = self.customData.get(API_AUTO_APPLY)
        self.apiCanOnlyApply = self.customData.get(API_CAN_ONLY_APPLY)
        self.apiAllowedInstanceNames = self.customData.get(
            API_ALLOWED_INSTANCE_NAMES)
        self.apiSchemaInstances = self.customData.get(API_SCHEMA_INSTANCES)
                               
        if self.apiSchemaType != MULTIPLE_APPLY:
            if self.propertyNamespacePrefix:
                raise _GetSchemaDefException(
                    "%s should only be used as a customData field on "
                    "multiple-apply API schemas." % PROPERTY_NAMESPACE_PREFIX,
                    sdfPrim.path)

            if self.apiAllowedInstanceNames:
                raise _GetSchemaDefException(
                    "%s should only be used as a customData field on "
                    "multiple-apply API schemas." % API_ALLOWED_INSTANCE_NAMES,
                    sdfPrim.path)

            if self.apiSchemaInstances:
                raise _GetSchemaDefException(
                    "%s should only be used as a customData field on "
                    "multiple-apply API schemas." % API_SCHEMA_INSTANCES,
                    sdfPrim.path)

        if self.isApi and \
           self.apiSchemaType not in API_SCHEMA_TYPE_TOKENS:
            raise _GetSchemaDefException(
                "CustomData 'apiSchemaType' is %s. It must be one of %s for an "
                "API schema." % (self.apiSchemaType, API_SCHEMA_TYPE_TOKENS),
                sdfPrim.path)

        if self.apiAutoApply and self.apiSchemaType != SINGLE_APPLY:
            raise _GetSchemaDefException("%s should only be used as a "
                "customData field on single-apply API schemas." % API_AUTO_APPLY,
                sdfPrim.path)

        self.isAppliedAPISchema = \
            self.apiSchemaType in [SINGLE_APPLY, MULTIPLE_APPLY]
        self.isMultipleApply = self.apiSchemaType == MULTIPLE_APPLY

        if self.apiCanOnlyApply and not self.isAppliedAPISchema:
            raise _GetSchemaDefException("%s should only be used as a "
                "customData field on applied API schemas." % API_CAN_ONLY_APPLY,
                sdfPrim.path)

        if self.isApi and not self.isAppliedAPISchema:
            self.schemaKind = "nonAppliedAPI";
        elif self.isApi and self.isAppliedAPISchema and not self.isMultipleApply:
            self.schemaKind = "singleApplyAPI"
        elif self.isApi and self.isAppliedAPISchema and self.isMultipleApply:
            self.schemaKind = "multipleApplyAPI"
        elif self.isConcrete and self.isTyped:
            self.schemaKind = "concreteTyped"
        elif self.isTyped:
            self.schemaKind = "abstractTyped"
        else:
            self.schemaKind = "abstractBase"
        self.schemaKindEnumValue = "UsdSchemaKind::" + _ProperCase(self.schemaKind)

        if self.isConcrete and not self.isTyped:
            raise _GetSchemaDefException('Schema classes must either inherit '
                                'Typed(IsA), or neither inherit typed '
                                'nor provide a typename(API).',
                                sdfPrim.path)

        if self.isApi and sdfPrim.path.name != "APISchemaBase" and \
            not sdfPrim.path.name.endswith('API'):
            raise _GetSchemaDefException(
                        'API schemas must be named with an API suffix.', 
                        sdfPrim.path)
        
        if self.isApi and sdfPrim.path.name != "APISchemaBase" and \
            (not self.parentCppClassName):
            raise _GetSchemaDefException(
                "API schemas must explicitly inherit from UsdAPISchemaBase.", 
                sdfPrim.path)

        if not self.isApi and self.isAppliedAPISchema:
            raise _GetSchemaDefException(
                'Non API schemas cannot have non-empty apiSchemaType value.', 
                sdfPrim.path)
        
        if self.fallbackPrimTypes and not self.isConcrete:
            raise _GetSchemaDefException(
                "fallbackPrimTypes can only be used as a customData field on "
                "concrete typed schema classes", sdfPrim.path)

    def GetHeaderFile(self):
        return self.baseFileName + '.h'

    def GetParentHeaderFile(self):
        return self.parentBaseFileName + '.h'

    def GetCppFile(self):
        return self.baseFileName + '.cpp'

    def GetWrapFile(self):
        return 'wrap' + self.className + '.cpp'


#------------------------------------------------------------------------------#
# USD PARSER                                                                   #
#------------------------------------------------------------------------------#

def _ValidateFields(spec):
    # The schema registry will ignore these fields if they are discovered 
    # in a generatedSchema.usda file, but we want to allow them in schema.usda.
    includeList = ["inheritPaths", "customData", "specifier"]

    invalidFields = [key for key in spec.ListInfoKeys()
        if Usd.SchemaRegistry.IsDisallowedField(key) and key not in includeList]
    if not invalidFields:
        return True

    for key in invalidFields:
        if key == Sdf.RelationshipSpec.TargetsKey:
            Print.Err("ERROR: Relationship targets on <%s> cannot be "
                      "specified in a schema." % spec.path)
        elif key == Sdf.AttributeSpec.ConnectionPathsKey:
            Print.Err("ERROR: Attribute connections on <%s> cannot be "
                      "specified in a schema." % spec.path)
        else:
            Print.Err("ERROR: Fallback values for '%s' on <%s> cannot be "
                      "specified in a schema." % (key, spec.path))
    return False

def GetClassInfo(classes, cppClassName):
    for c in classes:
        if c.cppClassName == cppClassName:
            return c
    return None

def ParseUsd(usdFilePath):
    sdfLayer = Sdf.Layer.FindOrOpen(usdFilePath)
    stage = Usd.Stage.Open(sdfLayer)
    classes = []

    hasInvalidFields = False

    # PARSE CLASSES
    for sdfPrim in sdfLayer.rootPrims:
        if sdfPrim.name == "Typed" or sdfPrim.specifier != Sdf.SpecifierClass:
            continue

        if not _ValidateFields(sdfPrim):
            hasInvalidFields = True

        usdPrim = stage.GetPrimAtPath(sdfPrim.path)
        classInfo = ClassInfo(usdPrim, sdfPrim)

        # make sure that if we have a multiple-apply schema with a property
        # namespace prefix that the prim actually has some properties
        if classInfo.apiSchemaType == MULTIPLE_APPLY:
            if classInfo.propertyNamespacePrefix and \
                len(sdfPrim.properties) == 0:
                    raise _GetSchemaDefException(
                        "Multiple-apply schemas that have the "
                        "propertyNamespacePrefix metadata fields must have at "
                        "least one property", sdfPrim.path)
            if not classInfo.propertyNamespacePrefix and \
                not len(sdfPrim.properties) == 0:
                    raise _GetSchemaDefException(
                        "Multiple-apply schemas that do not"
                        "have a propertyNamespacePrefix metadata field must "
                        "have zero properties", sdfPrim.path)

        classes.append(classInfo)
        #
        # We don't want to use the composed property names here because we only
        # want the local properties declared directly on the class, which the
        # "properties" metadata field provides.
        #
        attrApiNames = []
        relApiNames = []
        for sdfProp in sdfPrim.properties:

            if not _ValidateFields(sdfProp):
                hasInvalidFields = True

            # Attribute
            if isinstance(sdfProp, Sdf.AttributeSpec):
                attrInfo = AttrInfo(sdfProp)

                # Assert unique attribute names
                if (attrInfo.apiName != ''):
                    if attrInfo.name in classInfo.attrs:
                        raise Exception(
                            'Schema Attribute names must be unique, '
                            'irrespective of namespacing. '
                            'Duplicate name encountered: %s.%s' %
                            (classInfo.usdPrimTypeName, attrInfo.name))
                    elif attrInfo.apiName in attrApiNames:
                        raise Exception(
                            'Schema Attribute API names must be unique. '
                            'Duplicate apiName encountered: %s.%s' %
                            (classInfo.usdPrimTypeName, attrInfo.apiName))
                    else:
                        attrApiNames.append(attrInfo.apiName)
                classInfo.attrs[attrInfo.name] = attrInfo
                classInfo.attrOrder.append(attrInfo.name)

            # Relationship
            else:
                relInfo = RelInfo(sdfProp)

                # Assert unique relationship names
                if (relInfo.apiName != ''):
                    if relInfo.name in classInfo.rels: 
                        raise Exception(
                            'Schema Relationship names must be unique, '
                            'irrespective of namespacing. '
                            'Duplicate name encountered: %s.%s' %
                            (classInfo.usdPrimTypeName, relInfo.name))
                    elif relInfo.apiName in relApiNames:
                        raise Exception(
                            'Schema Relationship API names must be unique. '
                            'Duplicate apiName encountered: %s.%s' %
                            (classInfo.usdPrimTypeName, relInfo.apiName))
                    else:
                        relApiNames.append(relInfo.apiName)
                classInfo.rels[relInfo.name] = relInfo
                classInfo.relOrder.append(relInfo.name)

    
    for classInfo in classes:
        # If this is an applied API schema that does not inherit from 
        # UsdAPISchemaBase directly, ensure that the parent class is also 
        # an applied API Schema.
        if classInfo.isApi and classInfo.parentCppClassName!='UsdAPISchemaBase':
            parentClassInfo = GetClassInfo(classes, classInfo.parentCppClassName)
            if parentClassInfo:
                if parentClassInfo.isAppliedAPISchema != \
                        classInfo.isAppliedAPISchema:
                    raise Exception("API schema '%s' inherits from incompatible "
                        "base API schema '%s'. Both must be either applied API "
                        "schemas or non-applied API schemas." %
                        (classInfo.cppClassName, parentClassInfo.cppClassName))
                if parentClassInfo.isMultipleApply != \
                        classInfo.isMultipleApply:
                    raise Exception("API schema '%s' inherits from incompatible "
                        "base API schema '%s'. Both must be either single-apply "
                        "or multiple-apply." % (classInfo.cppClassName,
                        parentClassInfo.cppClassName))
            else:
                parentClassTfType = Tf.Type.FindByName(
                        classInfo.parentCppClassName)
                if parentClassTfType and parentClassTfType != Tf.Type.Unknown:
                    if classInfo.isAppliedAPISchema != \
                        Usd.SchemaRegistry().IsAppliedAPISchema(parentClassTfType):
                        raise Exception("API schema '%s' inherits from "
                            "incompatible base API schema '%s'. Both must be "
                            "either applied API schemas or non-applied API "
                            " schemas." % (classInfo.cppClassName,
                            parentClassInfo.cppClassName))
                    if classInfo.isMultipleApply != \
                        Usd.SchemaRegistry().IsMultipleApplyAPISchema(
                                parentClassTfType):
                        raise Exception("API schema '%s' inherits from "
                        "incompatible base API schema '%s'. Both must be either" 
                        " single-apply or multiple-apply." % 
                        (classInfo.cppClassName,  parentClassInfo.cppClassName))
        
    if hasInvalidFields:
        raise Exception('Invalid fields specified in schema.')

    return (_GetLibName(sdfLayer),
            _GetLibPath(sdfLayer),
            _GetLibPrefix(sdfLayer),
            _GetTokensPrefix(sdfLayer),
            _GetUseExportAPI(sdfLayer),
            _GetLibTokens(sdfLayer),
            _SkipCodeGenForSchemaLib(stage),
            classes)


#------------------------------------------------------------------------------#
# CODE GENERATOR                                                               #
#------------------------------------------------------------------------------#

def _WriteFile(filePath, content, validate):
    import difflib

    # If file currently exists and content is unchanged, do nothing.
    existingContent = '\n'
    content = (content + '\n'
               if content and not content.endswith('\n') else content)
    if os.path.exists(filePath):
        with open(filePath, 'r') as fp:
            existingContent = fp.read()
        if existingContent == content:
            Print('\tunchanged %s' % filePath)
            return

        # In validation mode, we just want to see if the code being generated
        # would differ from the code that currently exists without writing
        # anything out. So just generate a diff and bail out immediately.
        if validate:
            Print('Diff: ')
            Print('\n'.join(difflib.unified_diff(
                                existingContent.split('\n'),
                                content.split('\n'))))
            Print.Err('Error: validation failed, diffs found. '
                      'Please rerun %s.' % PROGRAM_NAME)
            sys.exit(1)
    else:
        if validate:
            Print.Err('Error: validation failed, file %s does not exist. '
                      'Please rerun %s.' % 
                      (os.path.basename(filePath), PROGRAM_NAME))
            sys.exit(1)

    # Otherwise attempt to write to file.
    try:
        with open(filePath, 'w') as curfile:
            curfile.write(content)
            Print('\t    wrote %s' % filePath)
    except IOError as ioe:
        Print.Err('\t ', ioe)

def _ExtractCustomCode(filePath, default=None):
    defaultTxt = default if default else ''
    
    if not os.path.exists(filePath):
        return defaultTxt

    try:
        with open(filePath, 'r') as src:
            existing = src.read()
            parts = existing.split('// --(BEGIN CUSTOM CODE)--\n')
            if len(parts) != 2 or not parts[1].strip():
                return defaultTxt
            return parts[1]
                
    except Exception as e:
        Print.Err(e)
        return defaultTxt


def _AddToken(tokenDict, tokenId, val, desc):
    """tokenId must be an identifier"""

    cppReservedKeywords = [
        "alignas", "alignof", "and", "and_eq", "asm", "atomic_cancel",
        "atomic_commit", "atomic_noexcept", "auto", "bitand", "bitor", "bool",
        "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
        "class", "compl", "concept", "const", "consteval", "constexpr",
        "constinit", "const_cast", "continue", "co_await", "co_return",
        "co_yield", "decltype", "default", "delete", "do", "double",
        "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false",
        "float", "for", "friend", "goto", "if", "inline", "int", "long",
        "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr",
        "operator", "or", "or_eq", "private", "protected", "public", "reflexpr",
        "register", "reinterpret_cast", "requires", "return", "short", "signed",
        "sizeof", "static", "static_assert", "static_cast", "struct", "switch",
        "synchronized", "template", "this", "thread_local", "throw", "true",
        "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
        "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
    ]
    pythonReservedKeywords = keyword.kwlist
    # If token is a reserved word in either language, append with underscore.
    # 'interface' is not a reserved word but is a macro on Windows when using
    # COM so we treat it as reserved.
    reserved = set(cppReservedKeywords + keyword.kwlist + [
        'interface',
    ])
    if tokenId in reserved:
        tokenId = tokenId + '_'
    if not Tf.IsValidIdentifier(tokenId):
        raise Exception(
            'Token identifiers must be actual C/python-style identifiers.  '
            '\"%s\" is not a valid identifier... for libraryTokens and '
            'schemaTokens, use the "value" field to specify the non-identifier '
            'token value.' % tokenId)

    if tokenId in tokenDict:
        token = tokenDict[tokenId]

        # Assert one-to-one mapping of token.id -> token.value
        if token.value != val:
            raise Exception(
             'Token identifiers must map to exactly one token value. '
             'One-to-Many mapping encountered: %s maps to \"%s\" and \"%s\"'
             % (token.id, token.value, val))
        
        # Update Description
        tokenDict[tokenId] = token._replace(
            desc=desc + ', ' + token.desc)
    
    else:
        tokenDict[tokenId] = Token(tokenId, val, desc)


def GatherTokens(classes, libName, libTokens):
    tokenDict = {}

    # Add tokens from all classes to the token set
    for cls in classes:
        # Add tokens from attributes to the token set
        #
        # We sort by name here to get a stable ordering when building up the
        # desc string below. The sort is reversed because items are prepended
        # to the description. Reversing this sort results in a forward sort in
        # the doc strings.
        for attr in sorted(cls.attrs.values(),
                           key=lambda a: a.name.lower(), reverse=True):

            # Add Attribute Names to token set
            cls.tokens.add(attr.name)
            _AddToken(tokenDict, attr.name, attr.rawName, cls.cppClassName)

            
            # Add default value (if token type) to token set
            if attr.typeName == Sdf.ValueTypeNames.Token and attr.fallback:
                fallbackName = _CamelCase(attr.fallback)
                if attr.apiName != '':
                    desc = 'Default value for %s::Get%sAttr()' % \
                           (cls.cppClassName, _ProperCase(attr.apiName))
                else:
                    desc = 'Default value for %s schema attribute %s' % \
                           (cls.cppClassName, attr.rawName)
                cls.tokens.add(fallbackName)
                _AddToken(tokenDict, fallbackName, attr.fallback, desc)
            
            # Add Allowed Tokens for this attribute to token set
            if attr.allowedTokens:
                for val in attr.allowedTokens:
                    # Empty string is a valid allowedTokens member,
                    # but do not declare a named literal for it.
                    if val != '':
                        tokenId = _CamelCase(val)
                        if attr.apiName != '':
                            desc = 'Possible value for %s::Get%sAttr()' % \
                                   (cls.cppClassName, _ProperCase(attr.apiName))
                        else:
                            desc = 'Possible value for %s schema attribute %s' % \
                                   (cls.cppClassName, attr.rawName)
                        cls.tokens.add(tokenId)
                        _AddToken(tokenDict, tokenId, val, desc)

        # Add tokens from relationships to the token set
        for rel in cls.rels.values():
            cls.tokens.add(rel.name)
            _AddToken(tokenDict, rel.name, rel.rawName, cls.cppClassName)
            
        # Add schema tokens to token set
        schemaTokens = cls.customData.get("schemaTokens", {})
        for token, tokenInfo in schemaTokens.items():
            cls.tokens.add(token)
            _AddToken(tokenDict, token, tokenInfo.get("value", token),
                      _SanitizeDoc(tokenInfo.get("doc", 
                          "Special token for the %s schema." % cls.cppClassName), ' '))

        # Add property namespace prefix token for multiple-apply API
        # schema to token set
        if cls.propertyNamespacePrefix:
            cls.tokens.add(cls.propertyNamespacePrefix)
            _AddToken(tokenDict, cls.propertyNamespacePrefix,
                      cls.propertyNamespacePrefix,
                      "Property namespace prefix for the %s schema." % cls.cppClassName)

    # Add library-wide tokens to token set
    for token, tokenInfo in libTokens.items():
        _AddToken(tokenDict, token, tokenInfo.get("value", token), 
                  _SanitizeDoc(tokenInfo.get("doc",
                      "Special token for the %s library." % libName), ' '))

    # Sort the list of tokens lexicographically. This pair of keys will provide
    # a case insensitive primary key and a case sensitive secondary key. That
    # way we keep a stable sort for tokens that differ only in case.
    return sorted(tokenDict.values(), key=lambda token: (token.id.lower(), token.id))


def GenerateCode(templatePath, codeGenPath, tokenData, classes, validate,
                 namespaceOpen, namespaceClose, namespaceUsing,
                 useExportAPI, env, headerTerminatorString):
    #
    # Load Templates
    #
    Print('Loading Templates from {0}'.format(templatePath))
    try:
        apiTemplate = env.get_template('api.h')
        headerTemplate = env.get_template('schemaClass.h')
        sourceTemplate = env.get_template('schemaClass.cpp')
        wrapTemplate = env.get_template('wrapSchemaClass.cpp')
        tokensHTemplate = env.get_template('tokens.h')
        tokensCppTemplate = env.get_template('tokens.cpp')
        tokensWrapTemplate = env.get_template('wrapTokens.cpp')
    except TemplateNotFound as tnf:
        raise RuntimeError("Template not found: {0}".format(str(tnf)))
    except TemplateSyntaxError as tse:
        raise RuntimeError("Syntax error in template {0} at line {1}: {2}"
                           .format(tse.filename, tse.lineno, tse.message))

    if useExportAPI:
        Print('Writing API:')
        _WriteFile(os.path.join(codeGenPath, 'api.h'),
                   apiTemplate.render(),
                   validate)
    
    if tokenData:
        Print('Writing Schema Tokens:')
        # tokens.h
        _WriteFile(os.path.join(codeGenPath, 'tokens.h'),
                   tokensHTemplate.render(tokens=tokenData), validate)
        # tokens.cpp
        _WriteFile(os.path.join(codeGenPath, 'tokens.cpp'),
                   tokensCppTemplate.render(tokens=tokenData), validate)
        # wrapTokens.cpp
        _WriteFile(os.path.join(codeGenPath, 'wrapTokens.cpp'),
                   tokensWrapTemplate.render(tokens=tokenData), validate)

    #
    # Generate Schema Class Files
    #
    Print('Generating Classes:')

            
    for cls in classes:
        hasTokenAttrs = any(
            [cls.attrs[attr].typeName == Sdf.ValueTypeNames.Token
             for attr in cls.attrs])

        # header file
        clsHFilePath = os.path.join(codeGenPath, cls.GetHeaderFile())

        # Wrap headerTerminatorString with new lines if it is non-empty.
        headerTerminatorString = headerTerminatorString.strip()
        if headerTerminatorString:
            headerTerminatorString = '\n%s\n' % headerTerminatorString

        customCode = _ExtractCustomCode(clsHFilePath,
                default='};\n\n%s\n%s' % (
                    namespaceClose, headerTerminatorString))
        _WriteFile(clsHFilePath,
                   headerTemplate.render(
                       cls=cls, hasTokenAttrs=hasTokenAttrs) + customCode,
                   validate)

        # source file
        clsCppFilePath = os.path.join(codeGenPath, cls.GetCppFile())
        customCode = _ExtractCustomCode(clsCppFilePath)
        _WriteFile(clsCppFilePath, 
                   sourceTemplate.render(cls=cls) + customCode,
                   validate)
        
        # wrap file
        clsWrapFilePath = os.path.join(codeGenPath, cls.GetWrapFile())

        if useExportAPI:
            customCode = _ExtractCustomCode(clsWrapFilePath, default=(
                                            '\nnamespace {\n'
                                            '\nWRAP_CUSTOM {\n}\n'
                                            '\n}'))
        else:
            customCode = _ExtractCustomCode(clsWrapFilePath, default='\nWRAP_CUSTOM {\n}\n')

        _WriteFile(clsWrapFilePath,
                   wrapTemplate.render(cls=cls) + customCode, validate)

# Updates the plugInfo class metadata clsDict with the API schema application
# metadata from the class
def _UpdatePlugInfoWithAPISchemaApplyInfo(clsDict, cls):

    if not cls.isAppliedAPISchema:
        return

    # List any auto apply to entries for the applied API schema.
    if cls.apiAutoApply:
        clsDict.update({API_AUTO_APPLY: list(cls.apiAutoApply)})

    # List any can only apply to entries for applied API schemas.
    if cls.apiCanOnlyApply:
        clsDict.update({API_CAN_ONLY_APPLY: list(cls.apiCanOnlyApply)})

    # List any allowed instance names for multiple apply schemas.
    if cls.apiAllowedInstanceNames:
        clsDict.update(
            {API_ALLOWED_INSTANCE_NAMES: list(cls.apiAllowedInstanceNames)})

    # Add any per instance name apply metadata in another dicitionary for 
    # multiple apply schemas.
    if cls.apiSchemaInstances:
        instancesDict = {}
        for k, v in cls.apiSchemaInstances.items():
            instance = {}
            # There can be canOnlyApplyTo metadata on a per isntance basis.
            if v.get(API_CAN_ONLY_APPLY):
                instance.update(
                    {API_CAN_ONLY_APPLY: list(v.get(API_CAN_ONLY_APPLY))})
            if instance:
                instancesDict.update({k : instance})
        if instancesDict:
            clsDict.update({API_SCHEMA_INSTANCES: instancesDict})

def GeneratePlugInfo(templatePath, codeGenPath, classes, validate, env):

    #
    # Load Templates
    #
    Print('Loading Templates from {0}'.format(templatePath))
    try:
        plugInfoTemplate = env.get_template('plugInfo.json')
    except TemplateNotFound as tnf:
        raise RuntimeError("Template not found: {0}".format(str(tnf)))
    except TemplateSyntaxError as tse:
        raise RuntimeError("Syntax error in template {0} at line {1}: {2}"
                           .format(tse.filename, tse.lineno, tse.message))

    #
    # Generate plugInfo.json.
    #
    if classes:
        import json
        # read existing plugInfo file, strip comments.
        plugInfoFile = os.path.join(codeGenPath, 'plugInfo.json')
        if os.path.isfile(plugInfoFile):
            with open(plugInfoFile, 'r') as fp:
                infoLines = fp.readlines()
            infoLines = [l for l in infoLines
                         if not l.strip().startswith('#')]
            # parse as json.
            try:
                info = json.loads(''.join(infoLines))
            except ValueError as ve:
                Print.Err('\t', 'reading', plugInfoFile)
        else:
            # use plugInfo.json template as starting point for new files,
            try:
                info = json.loads(plugInfoTemplate.render())
            except ValueError as ve:
                Print.Err('\t', ve, 'from template', plugInfoTemplate.filename)

        # pull the types dictionary.
        if 'Plugins' in info:
            for pluginData in info.get('Plugins', {}):
                if pluginData.get('Name') == env.globals['libraryName']:
                    types = (pluginData
                             .setdefault('Info', {})
                             .setdefault('Types', {}))
                    break
            else:
                Print.Err('\t', 'Could not find plugin metadata section for',
                          env.globals['libraryName'])
        else:
            types = info.setdefault('Types', {})
        # remove auto-generated types. Use a list to make a copy of keys before
        # we mutate it.
        for tname in list(types.keys()):
            if types[tname].get('autoGenerated', False):
                del types[tname]
        # generate types entries.
        for cls in classes:
            clsDict = dict()
            # add extraPlugInfo first to ensure it doesn't stomp over
            # any items we add below.
            if 'extraPlugInfo' in cls.customData:
                clsDict.update(cls.customData['extraPlugInfo'])
            clsDict.update({'bases': [cls.parentCppClassName],
                            'autoGenerated': True })

            clsDict.update({"schemaKind": cls.schemaKind})

            # Add all the API schema apply info fields to to the plugInfo. 
            # This may include auto-apply, can-only-apply, and allowed names 
            # for multiple apply schemas.
            _UpdatePlugInfoWithAPISchemaApplyInfo(clsDict, cls)

            # Write out alias/primdefs for concrete IsA schemas and API schemas
            if (cls.isTyped or cls.isApi):
                clsDict['alias'] = {'UsdSchemaBase': cls.usdPrimTypeName}

            types[cls.cppClassName] = clsDict
        # write plugInfo file back out.
        content = ((
            "# Portions of this file auto-generated by %s.\n"
            "# Edits will survive regeneration except for comments and\n"
            "# changes to types with autoGenerated=true.\n"
            % os.path.basename(os.path.splitext(sys.argv[0])[0])) + 
                   json.dumps(info, indent=4, sort_keys=True,
                              separators=(', ', ': ')))
        _WriteFile(plugInfoFile, content, validate)


def _MakeFlattenedRegistryLayer(filePath):
    # We flatten the schema classes to 'eliminate' composition when querying
    # the schema registry.
    stage = Usd.Stage.Open(filePath)

    # Certain qualities on builtin properties are not overridable in scene
    # description.  For example, builtin attributes' types always come from the
    # definition registry, never from scene description.  That's problematic in
    # this case where we're trying to *establish* the definition registry.
    # Since the classes in schema.usda use the real prim type names
    # (e.g. 'Mesh') Usd will pick up these built-in qualities from the existing
    # prim definition.  To side-step this, we temporarily override the prim type
    # names in the session layer so they do not pick up these special
    # nonoverridable qualities when we flatten.  Then after the fact we repair
    # all the type names.

    def mangle(typeName):
        return '__MANGLED_TO_AVOID_BUILTINS__' + typeName
    def demangle(typeName):
        return typeName.replace('__MANGLED_TO_AVOID_BUILTINS__', '')

    # Mangle the typeNames.
    with Usd.EditContext(stage, editTarget=stage.GetSessionLayer()):
        for cls in stage.GetPseudoRoot().GetAllChildren():
            if cls.GetTypeName():
                cls.SetTypeName(mangle(cls.GetTypeName()))

    flatLayer = stage.Flatten(addSourceFileComment=False)

    # Demangle the typeNames.
    for cls in flatLayer.rootPrims:
        if cls.typeName:
            cls.typeName = demangle(cls.typeName)

    # In order to prevent derived classes from inheriting base class
    # documentation metadata, we must manually replace docs here.
    for layer in stage.GetLayerStack():
        for cls in layer.rootPrims:
            flatCls = flatLayer.GetPrimAtPath(cls.path)
            if cls.HasInfo('documentation'):
                flatCls.SetInfo('documentation', cls.documentation)
            else:
                flatCls.ClearInfo('documentation')

    return flatLayer
    

def GenerateRegistry(codeGenPath, filePath, classes, validate, env):

    # Get the flattened layer to work with.
    flatLayer = _MakeFlattenedRegistryLayer(filePath)

    # Delete GLOBAL prim and other schema prim customData so it doesn't pollute
    # the schema registry.  Also remove any definitions we included from
    # lower-level schema modules.  We hop back up to the UsdStage API to do
    # so because it is more convenient for these kinds of operations.
    flatStage = Usd.Stage.Open(flatLayer)
    pathsToDelete = []
    primsToKeep = set(cls.usdPrimTypeName for cls in classes)
    if not flatStage.RemovePrim('/GLOBAL'):
        Print.Err("ERROR: Could not remove GLOBAL prim.")
    allMultipleApplyAPISchemaNamespaces = {}
    allFallbackSchemaPrimTypes = {}
    for p in flatStage.GetPseudoRoot().GetAllChildren():
        # If this is an API schema, check if it's applied and record necessary
        # information.
        if p.GetName() in primsToKeep and p.GetName().endswith('API'):
            apiSchemaType = p.GetCustomDataByKey(API_SCHEMA_TYPE)
            if apiSchemaType == MULTIPLE_APPLY:
                namespacePrefix = p.GetCustomDataByKey(PROPERTY_NAMESPACE_PREFIX)
                if namespacePrefix:
                    allMultipleApplyAPISchemaNamespaces[p.GetName()] = \
                        namespacePrefix
                elif not p.GetPropertyNames():
                    allMultipleApplyAPISchemaNamespaces[p.GetName()] = ""
                else:
                    raise _GetSchemaDefException("propertyNamespacePrefix "
                        "must exist as a metadata field on multiple-apply "
                        "API schemas with properties", p.GetPath())

            # API schema classes must not have authored metadata except for 
            # these exceptions:
            #   'documentation' - This is allowed
            #   'customData' - This will be deleted below
            #   'specifier' - This is a required field and will always exist.
            # Any other metadata is an error.
            allowedAPIMetadata = ['specifier', 'customData', 'documentation']
            invalidMetadata = [key for key in p.GetAllAuthoredMetadata().keys()
                               if key not in allowedAPIMetadata]
            if invalidMetadata:
                raise _GetSchemaDefException("Found invalid metadata fields "
                    "%s in API schema definition. API schemas cannot provide "
                    "prim metadata fallbacks" % str(invalidMetadata) , 
                    p.GetPath())

        if p.HasAuthoredTypeName():
            fallbackTypes = p.GetCustomDataByKey(FALLBACK_TYPES)
            if fallbackTypes:
                allFallbackSchemaPrimTypes[p.GetName()] = \
                    Vt.TokenArray(fallbackTypes)

        p.ClearCustomData()
        for myproperty in p.GetAuthoredProperties():
            myproperty.ClearCustomData()
        if p.GetName() not in primsToKeep:
            pathsToDelete.append(p.GetPath())
    for p in pathsToDelete:
        flatStage.RemovePrim(p)
        
    # Set layer's comment to indicate that the file is generated.
    flatLayer.comment = 'WARNING: THIS FILE IS GENERATED BY usdGenSchema. '\
                        ' DO NOT EDIT.'

    # Add the list of all applied and multiple-apply API schemas.
    if allMultipleApplyAPISchemaNamespaces:
        flatLayer.customLayerData = {
            'multipleApplyAPISchemas' : allMultipleApplyAPISchemaNamespaces
        }

    if allFallbackSchemaPrimTypes:
        flatLayer.GetPrimAtPath('/').SetInfo(Usd.Tokens.fallbackPrimTypes, 
                                             allFallbackSchemaPrimTypes)

    #
    # Generate Schematics
    #
    Print('Generating Schematics:')
    layerSource = flatLayer.ExportToString()

    # Remove doxygen tags from schema registry docs.
    # ExportToString escapes '\' again, so take that into account.
    layerSource = layerSource.replace(r'\\em ', '')
    layerSource = layerSource.replace(r'\\li', '-')
    layerSource = re.sub(r'\\+ref [^\s]+ ', '', layerSource)
    layerSource = re.sub(r'\\+section [^\s]+ ', '', layerSource)

    _WriteFile(os.path.join(codeGenPath, 'generatedSchema.usda'), layerSource,
               validate)

def InitializeResolver():
    """Initialize the resolver so that search paths pointing to schema.usda
    files are resolved to the directories where those files are installed"""
    
    from pxr import Ar, Plug

    # Force the use of the ArDefaultResolver so we can take advantage
    # of its search path functionality.
    Ar.SetPreferredResolver('ArDefaultResolver')

    # Figure out where all the plugins that provide schemas are located
    # and add their resource directories to the search path prefix list.
    resourcePaths = set()
    pr = Plug.Registry()
    for t in pr.GetAllDerivedTypes('UsdSchemaBase'):
        plugin = pr.GetPluginForType(t)
        if plugin:
            resourcePaths.add(plugin.resourcePath)
    
    # The sorting shouldn't matter here, but we do it for consistency
    # across runs.
    Ar.DefaultResolver.SetDefaultSearchPath(sorted(list(resourcePaths)))

if __name__ == '__main__':
    #
    # Parse Command-line
    #
    parser = ArgumentParser(description='Generate C++ schema class code from a '
        'USD file.')
    parser.add_argument('schemaPath',
        nargs='?',
        type=str,
        default='./schema.usda',
        help='The source USD file where schema classes are defined. '
        '[Default: %(default)s]')
    parser.add_argument('codeGenPath',
        nargs='?',
        type=str,
        default='.',
        help='The target directory where the code should be generated. '
        '[Default: %(default)s]')
    parser.add_argument('-v', '--validate',
        action='store_true',
        help='Verify that the source files are unchanged.')
    parser.add_argument('-q', '--quiet',
        action='store_true',
        help='Do not output text during execution.')
    parser.add_argument('-n', '--namespace',
        nargs='+',
        type=str,
        help='Wrap code in this specified namespace. Multiple arguments '
             'will be interpreted as a nested namespace. The leftmost '
             'argument will be treated as the outermost namespace, with '
             'the rightmost argument as the innermost.')

    defaultTemplateDir = os.path.join(
        os.path.dirname(
            os.path.abspath(inspect.getfile(inspect.currentframe()))),
        'codegenTemplates')

    instTemplateDir = os.path.join(
        os.path.abspath(Plug.Registry().GetPluginWithName('usd').resourcePath),
        'codegenTemplates')

    parser.add_argument('-t', '--templates',
        dest='templatePath',
        type=str,
        help=('Directory containing schema class templates. '
              '[Default: first directory that exists from this list: {0}]'
              .format(os.pathsep.join([defaultTemplateDir, instTemplateDir]))))

    parser.add_argument('-hts', '--headerTerminatorString',
        dest='headerTerminatorString',
        type=str,
        default='#endif',
        help=('The string used to terminate generated C++ header files, '
              'after the custom code section. '
              '[Default: %(default)s]'))

    args = parser.parse_args()
    codeGenPath = os.path.abspath(args.codeGenPath)
    schemaPath = os.path.abspath(args.schemaPath)

    if args.templatePath:
        templatePath = os.path.abspath(args.templatePath)
    else:
        if os.path.isdir(defaultTemplateDir):
            templatePath = defaultTemplateDir
        else:
            templatePath = instTemplateDir

    if args.namespace:
        namespaceOpen  = ' '.join('namespace %s {' % n for n in args.namespace)
        namespaceClose = '}'*len(args.namespace)
        namespaceUsing = ('using namespace ' 
                          + '::'.join(n for n in args.namespace) + ';')
    else:
        namespaceOpen  = 'PXR_NAMESPACE_OPEN_SCOPE'
        namespaceClose = 'PXR_NAMESPACE_CLOSE_SCOPE'
        namespaceUsing = 'PXR_NAMESPACE_USING_DIRECTIVE'

    Print.SetQuiet(args.quiet)

    #
    # Error Checking
    #
    if not os.path.isfile(schemaPath):
        Print.Err('Usage Error: First positional argument must be a USD schema file.')
        parser.print_help()
        sys.exit(1)
    if args.templatePath and not os.path.isdir(templatePath):
        Print.Err('Usage Error: templatePath argument must be the path to the codegenTemplates.')
        parser.print_help()
        sys.exit(1)

    try:
        #
        # Initialize the asset resolver to resolve search paths
        #
        InitializeResolver()
        
        #
        # Gather Schema Class information
        #
        libName, \
        libPath, \
        libPrefix, \
        tokensPrefix, \
        useExportAPI, \
        libTokens, \
        skipCodeGen, \
        classes = ParseUsd(schemaPath)
        tokenData = GatherTokens(classes, libName, libTokens)
        
        if args.validate:
            Print('Validation on, any diffs found will cause failure.')

        Print('Processing schema classes:')
        Print(', '.join(map(lambda self: self.usdPrimTypeName, classes)))

        #
        # Generate Code from Templates
        #
        if not os.path.isdir(codeGenPath):
            os.makedirs(codeGenPath)

        j2_env = Environment(loader=FileSystemLoader(templatePath),
                             trim_blocks=True)
        j2_env.globals.update(Camel=_CamelCase,
                              Proper=_ProperCase,
                              Upper=_UpperCase,
                              Lower=_LowerCase,
                              namespaceOpen=namespaceOpen,
                              namespaceClose=namespaceClose,
                              namespaceUsing=namespaceUsing,
                              libraryName=libName,
                              libraryPath=libPath,
                              libraryPrefix=libPrefix,
                              tokensPrefix=tokensPrefix,
                              useExportAPI=useExportAPI)

        # Generate code for schema libraries that aren't specified as codeless.
        if not skipCodeGen:
            GenerateCode(templatePath, codeGenPath, tokenData, classes, 
                         args.validate,
                         namespaceOpen, namespaceClose, namespaceUsing,
                         useExportAPI, j2_env, args.headerTerminatorString)
        # We always generate plugInfo and generateSchema.
        GeneratePlugInfo(templatePath, codeGenPath, classes, args.validate,
                         j2_env)
        GenerateRegistry(codeGenPath, schemaPath, classes, 
                         args.validate, j2_env)
    
    except Exception as e:
        Print.Err("ERROR:", str(e))
        sys.exit(1)
