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
/// \file alembicReader.cpp

#include "pxr/pxr.h"
#include "pxr/usd/plugin/usdAbc/alembicReader.h"
#include "pxr/usd/plugin/usdAbc/alembicUtil.h"
#include "pxr/usd/usdGeom/hermiteCurves.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/base/work/threadLimits.h"
#include "pxr/base/trace/trace.h"
#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/ostreamMethods.h"
#include <boost/type_traits/is_base_of.hpp>
#include <boost/utility/enable_if.hpp>
#include <Alembic/Abc/ArchiveInfo.h>
#include <Alembic/Abc/IArchive.h>
#include <Alembic/Abc/IObject.h>
#include <Alembic/Abc/ITypedArrayProperty.h>
#include <Alembic/Abc/ITypedScalarProperty.h>
#include <Alembic/AbcCoreAbstract/Foundation.h>
#include <Alembic/AbcCoreFactory/IFactory.h>
#include <Alembic/AbcGeom/GeometryScope.h>
#include <Alembic/AbcGeom/ICamera.h>
#include <Alembic/AbcGeom/ICurves.h>
#include <Alembic/AbcGeom/IPoints.h>
#include <Alembic/AbcGeom/IPolyMesh.h>
#include <Alembic/AbcGeom/ISubD.h>
#include <Alembic/AbcGeom/IXform.h>
#include <Alembic/AbcGeom/SchemaInfoDeclarations.h>
#include <Alembic/AbcGeom/Visibility.h>
#include <functional>
#include <memory>
#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE


// Define this to dump the namespace hierarchy as we traverse Alembic.
//#define USDABC_ALEMBIC_DEBUG

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (transform)
    ((xformOpTransform, "xformOp:transform"))
    ((indicesSuffix, ":indices"))
    ((valuesSuffix, ":vals"))
);

TF_DEFINE_ENV_SETTING(
    USD_ABC_WARN_ALL_UNSUPPORTED_VALUES, false,
    "Issue warnings for all unsupported values encountered.");

TF_DEFINE_ENV_SETTING(
    USD_ABC_NUM_OGAWA_STREAMS, 1,
    "The number of threads available for reading ogawa-backed files via UsdAbc.");

TF_DEFINE_ENV_SETTING(
    USD_ABC_WRITE_UV_AS_ST_TEXCOORD2FARRAY, true,
    "Switch to false to disable writing Alembic uv sets as primvars:st with "
    "type texCoord2fArray to USD");


TF_DEFINE_ENV_SETTING(
    USD_ABC_XFORM_PRIM_COLLAPSE, true,
    "Collapse Xforms containing a single geometry into a single geom Prim in USD");

#if ALEMBIC_LIBRARY_VERSION >= 10709
TF_DEFINE_ENV_SETTING(
    USD_ABC_READ_ARCHIVE_USE_MMAP, false,
    "Use mmap when reading from an Ogawa archive.");
#endif

namespace {

using namespace ::Alembic::AbcGeom;
using namespace UsdAbc_AlembicUtil;

static const TfToken&
_GetUVPropertyName()
{
    static const TfToken uvUsdAbcPropertyName = 
        (TfGetEnvSetting(USD_ABC_WRITE_UV_AS_ST_TEXCOORD2FARRAY)) ?
        (UsdAbcPropertyNames->st) : (UsdAbcPropertyNames->uv);
    return uvUsdAbcPropertyName;
}

static const SdfValueTypeName&
_GetUVTypeName()
{
    static const SdfValueTypeName uvTypeName = 
        (TfGetEnvSetting(USD_ABC_WRITE_UV_AS_ST_TEXCOORD2FARRAY)) ?
        (SdfValueTypeNames->TexCoord2fArray) : (SdfValueTypeNames->Float2Array);
    return uvTypeName;
}

static size_t
_GetNumOgawaStreams()
{
    return std::min(TfGetEnvSetting(USD_ABC_NUM_OGAWA_STREAMS),
                    static_cast<int>(WorkGetConcurrencyLimit()));
}

#if PXR_HDF5_SUPPORT_ENABLED && !H5_HAVE_THREADSAFE
// A global mutex until our HDF5 library is thread safe.  It has to be
// recursive to handle the case where we write an Alembic file using an
// UsdAbc_AlembicData as the source.
static TfStaticData<std::recursive_mutex> _hdf5;
#endif // PXR_HDF5_SUPPORT_ENABLED

// The SdfAbstractData time samples type.
// XXX: SdfAbstractData should typedef this.
typedef std::set<double> UsdAbc_TimeSamples;

// A vector of Alembic times.
typedef std::vector<chrono_t> _AlembicTimeSamples;

class _ReaderContext;
class _PrimReaderContext;

//
// Error / warning message helpers
//

// Helper function to produce a string representing the path
// to an Alembic property.
static std::string
_GetAlembicPath(const IScalarProperty& p)
{
    std::vector<std::string> names;
    names.push_back(p.getName());

    for (ICompoundProperty prop = p.getParent(); 
         prop.valid(); prop = prop.getParent()) {
        names.push_back(prop.getName());
    }

    const std::string propName = TfStringJoin(names.rbegin(), names.rend(),".");

    std::string path = p.getObject().getFullName();
    if (!propName.empty() && propName[0] != '.') {
        path += '.';
    }
    path += propName;
    
    return path;
}

// Produce a string description of the sample selector
static std::string
_GetSampleSelectorDescription(const ISampleSelector& iss)
{
    if (iss.getRequestedIndex() == -1) {
        return "sample time " + TfStringify(iss.getRequestedTime());
    }
    return "sample index " + TfStringify(iss.getRequestedIndex());
}

enum _WarningType
{
    WarningVisibility = 0,
    WarningSubdivisionScheme,
    WarningInterpolateBoundary,
    WarningFaceVaryingInterpolateBoundary
};

static const char* _WarningNames[] = 
{
    "visibility",
    "subdivision scheme",
    "interpolate boundary",
    "face varying interpolate boundary"
};

static void
_PostUnsupportedValueWarning(
    const IScalarProperty& property,
    const ISampleSelector& iss,
    _WarningType warning, 
    const std::string& authoredValue,
    const std::string& replacementValue)
{
    const IObject object = property.getObject();
    const std::string archiveName = object.getArchive().getName();

    if (TfGetEnvSetting(USD_ABC_WARN_ALL_UNSUPPORTED_VALUES)) {
        TF_WARN(
            "Unsupported %s '%s' for <%s> at %s in archive '%s'. "
            "Using '%s' instead.", 
            _WarningNames[warning],
            authoredValue.c_str(),
            _GetAlembicPath(property).c_str(), 
            _GetSampleSelectorDescription(iss).c_str(),
            archiveName.c_str(),
            replacementValue.c_str());
        return;
    }

    typedef std::pair<_WarningType, std::string> _WarningAndArchive;
    typedef std::set<_WarningAndArchive> _IssuedWarnings;

    static _IssuedWarnings warnings;
    static std::mutex mutex;

    bool issueWarning = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        const _WarningAndArchive newWarning(warning, archiveName);
        issueWarning = warnings.insert(newWarning).second;
    }

    if (issueWarning) {
        TF_WARN(
            "Unsupported %s detected in archive '%s'. Using '%s' instead.",
            _WarningNames[warning], archiveName.c_str(),
            replacementValue.c_str());
    }
}

//
// Name helpers
//

struct _AlembicFixName {
    std::string operator()(std::string const &x) const {
        return TfMakeValidIdentifier(x);
    }
};
struct _AlembicFixNamespacedName {
    std::string operator()(std::string const &x) const {
        auto elems = TfStringSplit(x, ":");
        std::transform(elems.begin(), elems.end(), elems.begin(),
                       TfMakeValidIdentifier);
        return TfStringJoin(elems, ":");
    }
};

template <class T>
static
std::string
_CleanName(
    const std::string& inName,
    const char* trimLeading,
    const std::set<std::string>& usedNames,
    T fixer,
    bool (*test)(const std::string&) = &SdfPath::IsValidIdentifier)
{
    // Just return the name if it doesn't need mangling.  We assume the
    // client has prepopulated usedNames with all Alembic names in the group.
    if (test(inName)) {
        return TfToken(inName);
    }

    // Mangle name into desired form.

    // Handle empty name.
    std::string name = inName;
    if (name.empty()) {
        name = '_';
    }
    else {
        // Trim leading.
        name = TfStringTrimLeft(name, trimLeading);

        // If name is not a valid identifier then substitute characters.
        if (!test(name)) {
            name = fixer(name);
        }
    }

    // Now check against usedNames.
    if (usedNames.find(name) != usedNames.end()) {
        // Just number the tries.
        int i = 0;
        std::string attempt = TfStringPrintf("%s_%d", name.c_str(), ++i);
        while (usedNames.find(attempt) != usedNames.end()) {
            attempt = TfStringPrintf("%s_%d", name.c_str(), ++i);
        }
        name = attempt;
    }

    return name;
}

//
// Metadata helpers
//

// A map of metadata.
typedef std::map<TfToken, VtValue> MetadataMap;

/// Returns the Alembic metadata name for a Usd metadata field name.
static
std::string
_AmdName(const std::string& name)
{
    return "Usd:" + name;
}

static
void
_GetBoolMetadata(
    const MetaData& alembicMetadata,
    MetadataMap& usdMetadata,
    const TfToken& field)
{
    std::string value = alembicMetadata.get(_AmdName(field));
    if (!value.empty()) {
        usdMetadata[field] = VtValue(value == "true");
    }
}

static
void
_GetStringMetadata(
    const MetaData& alembicMetadata,
    MetadataMap& usdMetadata,
    const TfToken& field)
{
    std::string value = alembicMetadata.get(_AmdName(field));
    if (!value.empty()) {
        usdMetadata[field] = VtValue(value);
    }
}

static
void
_GetTokenMetadata(
    const MetaData& alembicMetadata,
    MetadataMap& usdMetadata,
    const TfToken& field)
{
    std::string value = alembicMetadata.get(_AmdName(field));
    if (!value.empty()) {
        usdMetadata[field] = VtValue(TfToken(value));
    }
}

static
void
_GetDoubleMetadata(
    const MetaData& alembicMetadata,
    MetadataMap& usdMetadata,
    const TfToken& field)
{
    std::string value = alembicMetadata.get(_AmdName(field));
    if (!value.empty()) {
        char* end;
        const double v = strtod(value.c_str(), &end);
        if (*end == '\0') {
            usdMetadata[field] = VtValue(v);
        }
    }
}

//
// AlembicProperty
//

// Helpers for \c AlembicProperty.
template <class T>
struct _AlembicPropertyHelper {
//  T operator()(const ICompoundProperty& parent, const std::string& name)const;
};
template <>
struct _AlembicPropertyHelper<ICompoundProperty> {
    ICompoundProperty
    operator()(const ICompoundProperty& parent, const std::string& name,
               SchemaInterpMatching matching = kStrictMatching) const
    {
        if (const PropertyHeader* header = parent.getPropertyHeader(name)) {
            if (header->isCompound()) {
                return ICompoundProperty(parent, name);
            }
        }
        return ICompoundProperty();
    }
};
template <>
struct _AlembicPropertyHelper<IScalarProperty> {
    IScalarProperty
    operator()(const ICompoundProperty& parent, const std::string& name,
               SchemaInterpMatching matching = kStrictMatching) const
    {
        if (const PropertyHeader* header = parent.getPropertyHeader(name)) {
            if (header->isScalar()) {
                return IScalarProperty(parent, name);
            }
        }
        return IScalarProperty();
    }
};
template <class T>
struct _AlembicPropertyHelper<ITypedScalarProperty<T> > {
    ITypedScalarProperty<T>
    operator()(const ICompoundProperty& parent, const std::string& name,
               SchemaInterpMatching matching = kStrictMatching) const
    {
        if (const PropertyHeader* header = parent.getPropertyHeader(name)) {
            if (ITypedScalarProperty<T>::matches(*header, matching)) {
                return ITypedScalarProperty<T>(parent, name);
            }
        }
        return ITypedScalarProperty<T>();
    }
};
template <>
struct _AlembicPropertyHelper<IArrayProperty> {
    IArrayProperty
    operator()(const ICompoundProperty& parent, const std::string& name,
               SchemaInterpMatching matching = kStrictMatching) const
    {
        if (const PropertyHeader* header = parent.getPropertyHeader(name)) {
            if (header->isArray()) {
                return IArrayProperty(parent, name);
            }
        }
        return IArrayProperty();
    }
};
template <class T>
struct _AlembicPropertyHelper<ITypedArrayProperty<T> > {
    ITypedArrayProperty<T>
    operator()(const ICompoundProperty& parent, const std::string& name,
               SchemaInterpMatching matching = kStrictMatching) const
    {
        if (const PropertyHeader* header = parent.getPropertyHeader(name)) {
            if (ITypedArrayProperty<T>::matches(*header, matching)) {
                return ITypedArrayProperty<T>(parent, name);
            }
        }
        return ITypedArrayProperty<T>();
    }
};
template <class T>
struct _AlembicPropertyHelper<ITypedGeomParam<T> > {
    ITypedGeomParam<T>
    operator()(const ICompoundProperty& parent, const std::string& name,
               SchemaInterpMatching matching = kStrictMatching) const
    {
        if (const PropertyHeader* header = parent.getPropertyHeader(name)) {
            if (ITypedGeomParam<T>::matches(*header, matching)) {
                return ITypedGeomParam<T>(parent, name);
            }
        }
        return ITypedGeomParam<T>();
    }
};

/// \class AlembicProperty
/// \brief Wraps an Alembic property of any type.
///
/// An object of this type can hold any Alembic property but it must be
/// cast to get a concrete property object.  The client must know what
/// to cast to but the client can get the property header that describes
/// the held data.
class AlembicProperty {
public:
    AlembicProperty(const SdfPath& path, const std::string& name);
    AlembicProperty(const SdfPath& path, const std::string& name,
                    const IObject& parent);
    AlembicProperty(const SdfPath& path, const std::string& name,
                    const ICompoundProperty& parent);

    /// Returns the parent compound property.
    ICompoundProperty GetParent() const;

    /// Returns the name of the property.
    const std::string& GetName() const;

    /// Get the property header.  This returns \c NULL if the property
    /// doesn't exist.
    const PropertyHeader* GetHeader() const;

    /// This is the only way to get an actual Alembic property object.
    /// You must supply the expected type.  If it's incorrect then
    /// you'll get an object of the requested type but its valid()
    /// method will return \c false.
    template <class T>
    T Cast(SchemaInterpMatching matching = kStrictMatching) const
    {
        if (_parent.valid()) {
            return _AlembicPropertyHelper<T>()(_parent, _name, matching);
        }
        else {
            return T();
        }
    }

private:
    SdfPath _path;
    ICompoundProperty _parent;
    std::string _name;
};

AlembicProperty::AlembicProperty(
    const SdfPath& path,
    const std::string& name) :
    _path(path), _parent(), _name(name)
{
    // Do nothing
}

AlembicProperty::AlembicProperty(
    const SdfPath& path,
    const std::string& name,
    const IObject& parent) :
    _path(path), _parent(parent.getProperties()), _name(name)
{
    // Do nothing
}

AlembicProperty::AlembicProperty(
    const SdfPath& path,
    const std::string& name,
    const ICompoundProperty& parent) :
    _path(path), _parent(parent), _name(name)
{
    // Do nothing
}

ICompoundProperty
AlembicProperty::GetParent() const
{
    return _parent;
}

const std::string&
AlembicProperty::GetName() const
{
    return _name;
}

const PropertyHeader*
AlembicProperty::GetHeader() const
{
    return _parent.valid() ? _parent.getPropertyHeader(_name) : NULL;
}

//
// _ReaderSchema
//

/// \class _ReaderSchema
/// \brief The Usd to Alembic schema.
///
/// This class stores functions to read a Usd prim from Alembic keyed by
/// type.  Each type can have multiple readers, each affected by the
/// previous via a \c _PrimReaderContext.
class _ReaderSchema {
public:
    typedef std::function<void (_PrimReaderContext*)> PrimReader;
    typedef std::vector<PrimReader> PrimReaderVector;
    typedef UsdAbc_AlembicDataConversion::ToUsdConverter Converter;

    _ReaderSchema();

    /// Returns the prim readers for the given Alembic schema.  Returns an
    /// empty vector if the schema isn't known.
    const PrimReaderVector& GetPrimReaders(const std::string& schema) const;

    // Helper for defining types.
    class TypeRef {
    public:
        TypeRef(PrimReaderVector* readers) : _readerVector(readers) { }

        TypeRef& AppendReader(const PrimReader& reader)
        {
            _readerVector->push_back(reader);
            return *this;
        }

    private:
        PrimReaderVector* _readerVector;
    };

    /// Adds a type and returns a helper for defining it.
    template <class T>
    TypeRef AddType(T name)
    {
        return TypeRef(&_readers[name]);
    }

    /// Adds the fallback type and returns a helper for defining it.
    TypeRef AddFallbackType()
    {
        return AddType(std::string());
    }

    /// Returns the object holding the conversions registry.
    const UsdAbc_AlembicDataConversion& GetConversions() const
    {
        return _conversions.data;
    }

private:
    const UsdAbc_AlembicConversions _conversions;

    typedef std::map<std::string, PrimReaderVector> _ReaderMap;
    _ReaderMap _readers;
};

_ReaderSchema::_ReaderSchema()
{
    // Do nothing
}

const _ReaderSchema::PrimReaderVector&
_ReaderSchema::GetPrimReaders(const std::string& schema) const
{
    _ReaderMap::const_iterator i = _readers.find(schema);
    if (i != _readers.end()) {
        return i->second;
    }
    i = _readers.find(std::string());
    if (i != _readers.end()) {
        return i->second;
    }
    static const PrimReaderVector empty;
    return empty;
}

/// \class _ReaderContext
/// \brief The Alembic to Usd writer context.
///
/// This object holds information used by the writer for a given archive
/// and Usd data.
class _ReaderContext {
public:
    /// Gets data from some property at a given sample.
    typedef std::function<bool (const UsdAbc_AlembicDataAny&,
                                const ISampleSelector&)> Converter;

    /// An optional ordering of name children or properties.
    typedef boost::optional<TfTokenVector> Ordering;

    /// Sample times.
    typedef UsdAbc_AlembicDataReader::TimeSamples TimeSamples;

    /// Sample index.
    typedef UsdAbc_AlembicDataReader::Index Index;

    /// Property cache.
    struct Property {
        SdfValueTypeName typeName;
        MetadataMap metadata;
        TimeSamples sampleTimes;
        bool timeSampled;
        bool uniform;
        Converter converter;
    };
    typedef std::map<TfToken, Property> PropertyMap;

    /// Prim cache.
    struct Prim {
        Prim() : instanceable(false), promoted(false) { }

        TfToken typeName;
        TfTokenVector children;
        TfTokenVector properties;
        SdfSpecifier specifier;
        Ordering primOrdering;
        Ordering propertyOrdering;
        MetadataMap metadata;
        PropertyMap propertiesCache;
        SdfPath prototype;           // Path to prototype; only set on instances
        std::string instanceSource;  // Alembic path to instance source;
                                     // only set on prototype.
        bool instanceable;           // Instanceable; only set on prototype.
        bool promoted;               // True if a promoted instance/prototype
    };

    _ReaderContext();

    /// \name Reader setup
    /// @{

    /// Open an archive.
    bool Open(const std::string& filePath, std::string* errorLog,
              const SdfFileFormat::FileFormatArguments& args);

    /// Close the archive.
    void Close();

    /// Sets the reader schema.
    void SetSchema(const _ReaderSchema* schema) { _schema = schema; }

    /// Returns the reader schema.
    const _ReaderSchema& GetSchema() const { return *_schema; }

    /// Sets or resets the flag named \p flagName.
    void SetFlag(const TfToken& flagName, bool set);

    /// @}
    /// \name Reader caching
    /// @{

    /// Returns \c true iff a flag is in the set.
    bool IsFlagSet(const TfToken& flagName) const;

    /// Creates and returns the prim cache for path \p path.
    Prim& AddPrim(const SdfPath& path);

    /// Returns \c true if \p object is an instance in Usd (i.e. it's an
    /// instance in Alembic or is the source of an instance).
    bool IsInstance(const IObject& object) const;

    /// Creates and returns the prim cache for path \p path that's an
    /// instance of \p object.
    Prim& AddInstance(const SdfPath& path, const IObject& object);

    /// Creates and returns the property cache for path \p path.
    Property& FindOrCreateProperty(const SdfPath& path);

    /// Returns the property cache for path \p path if it exists, otherwise
    /// \c NULL.
    const Property* FindProperty(const SdfPath& path);

    /// Returns the sample times converted to Usd.
    TimeSamples ConvertSampleTimes(const _AlembicTimeSamples&) const;

    /// Add the given sample times to the global set of sample times.
    void AddSampleTimes(const TimeSamples&);

    /// @}
    /// \name SdfAbstractData access
    /// @{

    /// Test for the existence of a spec at \p id.
    bool HasSpec(const SdfPath& path) const;

    /// Returns the spec type for the spec at \p id.
    SdfSpecType GetSpecType(const SdfPath& path) const;

    /// Test for the existence of and optionally return the value at
    /// (\p path,\p fieldName).
    bool HasField(const SdfPath& path,
                  const TfToken& fieldName,
                  const UsdAbc_AlembicDataAny& value) const;

    /// Test for the existence of and optionally return the value of the
    /// property at \p id at index \p index.
    bool HasValue(const SdfPath& path, Index index,
                  const UsdAbc_AlembicDataAny& value) const;

    /// Visit the specs.
    void VisitSpecs(const SdfAbstractData& owner,
                    SdfAbstractDataSpecVisitor* visitor) const;

    /// List the fields.
    TfTokenVector List(const SdfPath& path) const;

    /// Returns the sampled times over all properties.
    const UsdAbc_TimeSamples& ListAllTimeSamples() const;

    /// Returns the sampled times for the property with id \p id.
    const TimeSamples& 
    ListTimeSamplesForPath(const SdfPath& path) const;

    /// @}

private:
    typedef AbcA::ObjectReaderPtr _ObjectPtr;
    typedef std::set<_ObjectPtr> _ObjectReaderSet;
    typedef std::map<_ObjectPtr, _ObjectReaderSet> _SourceToInstancesMap;

    // Walk the object hierarchy looking for instances and instance sources.
    static void _FindInstances(const IObject& parent,
                               _SourceToInstancesMap* instances);

    // Add to promotable those instance sources that are promotable.
    static void _FindPromotable(const _SourceToInstancesMap& instances,
                                _ObjectReaderSet* promotable);

    // Store instancing state.
    void _SetupInstancing(const _SourceToInstancesMap& instances,
                          const _ObjectReaderSet& promotable,
                          std::set<std::string>* usedNames);

    // Clear caches.
    void _Clear();

    const Prim* _GetPrim(const SdfPath& path) const;
    const Property* _GetProperty(const Prim&,
                                 const SdfPath& path) const;
    bool _HasField(const Prim* prim,
                   const TfToken& fieldName,
                   const UsdAbc_AlembicDataAny& value) const;
    bool _HasField(const Property* property,
                   const TfToken& fieldName,
                   const UsdAbc_AlembicDataAny& value) const;
    bool _HasValue(const Property* property,
                   const ISampleSelector& selector,
                   const UsdAbc_AlembicDataAny& value) const;

    // Custom auto-lock that safely ignores a NULL pointer.
    class _Lock : boost::noncopyable {
    public:
        _Lock(std::recursive_mutex* mutex) : _mutex(mutex) {
            if (_mutex) _mutex->lock();
        }
        ~_Lock() { if (_mutex) _mutex->unlock(); }

    private:
        std::recursive_mutex* _mutex;
    };

private:
    // The mutex to lock when reading the archive.  This is NULL except
    // for HDF5.
    mutable std::recursive_mutex* _mutex;

    // Conversion options.
    double _timeScale;              // Scale Alembic time by this factor.
    double _timeOffset;             // Offset Alembic->Usd time (after scale).
    std::set<TfToken, TfTokenFastArbitraryLessThan> _flags;

    // Input state.
    IArchive _archive;
    const _ReaderSchema* _schema;

    // A map of the full names of known instance sources.  The mapped
    // value has the Usd prototype path and whether we're using the actual
    // instance source or its parent.  We use the parent in some cases
    // in order to get more sharing in the Usd world.
    struct _PrototypeInfo {
        SdfPath path;
        bool promoted;
    };
    typedef std::map<std::string, _PrototypeInfo> _SourceToPrototypeMap;
    _SourceToPrototypeMap _instanceSources;

    // A map of full name of instance to full name of source.  If we're
    // using the parent of the source as the prototype then the source path
    // is the parent of the actual source and the instance path is the
    // parent of the actual instance.
    typedef std::map<std::string, std::string> _InstanceToSourceMap;
    _InstanceToSourceMap _instances;

    // Caches.
    typedef std::map<SdfPath, Prim> _PrimMap;
    _PrimMap _prims;
    Prim* _pseudoRoot;
    UsdAbc_TimeSamples _allTimeSamples;
};

static
void
_ReadPrimChildren(
    _ReaderContext& context,
    const IObject& object,
    const SdfPath& path,
    _ReaderContext::Prim& prim);

_ReaderContext::_ReaderContext() :
    _mutex(NULL),
    _timeScale(24.0),               // Usd is frames, Alembic is seconds.
    _timeOffset(0.0),               // Time 0.0 to frame 0.
    _schema(NULL)
{
    // Do nothing
}

bool
_ReaderContext::Open(const std::string& filePath, std::string* errorLog,
                     const SdfFileFormat::FileFormatArguments& args)
{
    Close();

    std::vector<std::string> layeredABC;
    {
        auto abcLayers = args.find("abcLayers");
        if (abcLayers != args.end()) {
            for (auto&& l : TfStringSplit(abcLayers->second, ",")) {
                layeredABC.emplace_back(std::move(l));
            }
        }
    }
    layeredABC.emplace_back(filePath);

#if PXR_HDF5_SUPPORT_ENABLED && !H5_HAVE_THREADSAFE
    // HDF5 may not be thread-safe.
    using lock_guard = std::lock_guard<std::recursive_mutex>;
    std::unique_ptr<std::lock_guard<std::recursive_mutex>> hfd5Lock(new lock_guard(*_hdf5));
#endif

    using IFactory = ::Alembic::AbcCoreFactory::IFactory;
    IFactory factory;
    IFactory::CoreType abcType;
    factory.setPolicy(Abc::ErrorHandler::Policy::kQuietNoopPolicy);
    factory.setOgawaNumStreams(_GetNumOgawaStreams());
    IArchive archive = factory.getArchive(layeredABC, abcType);

#if PXR_HDF5_SUPPORT_ENABLED && !H5_HAVE_THREADSAFE
    if (abcType == IFactory::kHDF5 || abcType == IFactory::kLayer) {
        // An HDF5, or layered which may have an HDF5 layer
        _mutex = &*_hdf5;
    } else {
        // Don't need the HDF5 lock
        hfd5Lock.reset();
    }
#endif

    std::string format;
    switch (abcType) {
        case IFactory::kHDF5: format = "HDF5"; break;
        case IFactory::kOgawa: format = "Ogawa"; break;
        case IFactory::kLayer: format = "Layer"; break;
        default:
        case IFactory::kUnknown: format = "Unknown"; break;
    }
    if (!archive.valid()) {
        *errorLog = TfStringPrintf("Unsupported format: '%s'", format.c_str());
        return false;
    }

    // Get info.
    uint32_t apiVersion;
    std::string writer, version, date, comment;
    GetArchiveInfo(archive, writer, version, apiVersion, date, comment);

    // Report.
    if (IsFlagSet(UsdAbc_AlembicContextFlagNames->verbose)) {
        TF_STATUS("Opened %s file written by Alembic %s",
                  format.c_str(),
                  UsdAbc_FormatAlembicVersion(apiVersion).c_str());
    }

    // Cut over.
    _archive = std::move(archive);

    // Fill pseudo-root in the cache.
    const SdfPath rootPath = SdfPath::AbsoluteRootPath();
    _pseudoRoot = &_prims[rootPath];
    _pseudoRoot->metadata[SdfFieldKeys->Documentation] = comment;

    // Gather the names of the root prims.  Instancing will want to create
    // new root prims.  Those must have unique names that don't modify the
    // names of existing root prims.  So we have to have the existing names
    // first.
    IObject root = _archive.getTop();
    std::set<std::string> usedRootNames;
    for (size_t i = 0, n = root.getNumChildren(); i != n; ++i) {
        IObject child(root, root.getChildHeader(i).getName());
        std::string name = _CleanName(child.getName(), " _", usedRootNames,
                                      _AlembicFixName(),
                                      &SdfPath::IsValidIdentifier);
        usedRootNames.insert(name);
    }

    // Fetch authored timeCodesPerSecond early so that we can use it 
    // for rescaling timeSamples.
    if (const PropertyHeader* property =
            root.getProperties().getPropertyHeader("Usd")) {
        const MetaData& metadata = property->getMetaData();
        _pseudoRoot->metadata[SdfFieldKeys->TimeCodesPerSecond] = 24.0;
       _GetDoubleMetadata(metadata, _pseudoRoot->metadata,
                           SdfFieldKeys->TimeCodesPerSecond);
       _timeScale = _pseudoRoot->metadata[SdfFieldKeys->TimeCodesPerSecond].Get<double>();
    }

    // Collect instancing information.
    // Skipping this step makes later code expand instances.
    if (!IsFlagSet(UsdAbc_AlembicContextFlagNames->expandInstances)) {
        // Find the instance sources and their instances.
        _SourceToInstancesMap instances;
        _FindInstances(root, &instances);

        // If we're allowing instance promotion find the candidates.
        _ObjectReaderSet promotable;
        if (IsFlagSet(UsdAbc_AlembicContextFlagNames->promoteInstances)) {
            _FindPromotable(instances, &promotable);
        }

        // Save instancing info to lookup during main traversal, including
        // choosing paths for the prototypes.
        _SetupInstancing(instances, promotable, &usedRootNames);
    }

    // Re-root so the <defaultPrim> is actually the archive!
    TfToken abcReRoot;
    {
        auto reRoot = args.find("abcReRoot");
        if (reRoot != args.end()) {
            if (!TfIsValidIdentifier(reRoot->second)) {
                TF_WARN("[usdAbc] Ignoring re-root because identifer '%s' is"
                        " not valid (%s).", reRoot->second.c_str(),
                        filePath.c_str());
            } else
                abcReRoot = TfToken(reRoot->second);
        }
    }

    // Fill rest of the cache.
    if (!abcReRoot.IsEmpty()) {
        SdfPath compPath = rootPath.AppendChild(abcReRoot);
        auto& xform = _prims[compPath];
        xform.typeName = UsdAbcPrimTypeNames->Xform;
        xform.specifier = SdfSpecifierDef;
        _ReadPrimChildren(*this, root, compPath, xform);
        _pseudoRoot->children.emplace_back(std::move(abcReRoot));
    } else
        _ReadPrimChildren(*this, root, rootPath, *_pseudoRoot);

    // Append the prototypes to the pseudo-root.  We use lexicographical order
    // but the order doesn't really matter.  We also note here the Alembic
    // source path for each prototype and whether it's instanceable or not
    // (which is chosen simply by the disableInstancing flag).
    if (!_instanceSources.empty()) {
        const bool instanceable =
            !IsFlagSet(UsdAbc_AlembicContextFlagNames->disableInstancing);
        std::map<SdfPath, std::string> prototypes;
        for (const _SourceToPrototypeMap::value_type& v: _instanceSources) {
            prototypes[v.second.path] = v.first;
        }
        for (const auto& prototype: prototypes) {
            const SdfPath& name = prototype.first;
            _pseudoRoot->children.push_back(name.GetNameToken());
            _prims[name].instanceSource = prototype.second;
            _prims[name].instanceable   = instanceable;
        }
    }

    // Guess the start and end timeCode using the sample times.
    if (!_allTimeSamples.empty()) {
        _pseudoRoot->metadata[SdfFieldKeys->StartTimeCode] =
            *_allTimeSamples.begin();
        _pseudoRoot->metadata[SdfFieldKeys->EndTimeCode]   =
            *_allTimeSamples.rbegin();
    }

    // If no upAxis is authored, pretend that it was authored as 'Y'.  This
    // is primarily to facilitate working with externally-generated abc
    // files at Pixar, where we unfortunately still work in a Z-up pipeline,
    // and therefore configure UsdGeomGetFallbackUpAxis() to return 'Z'.
    _pseudoRoot->metadata[UsdGeomTokens->upAxis] = UsdGeomTokens->y;

    // Get the Usd metadata.  This will overwrite any metadata previously
    // set on _pseudoRoot.
    if (const PropertyHeader* property =
            root.getProperties().getPropertyHeader("Usd")) {
        const MetaData& metadata = property->getMetaData();
        _GetDoubleMetadata(metadata, _pseudoRoot->metadata,
                           SdfFieldKeys->StartTimeCode);
        _GetDoubleMetadata(metadata, _pseudoRoot->metadata,
                           SdfFieldKeys->EndTimeCode);

        _GetDoubleMetadata(metadata, _pseudoRoot->metadata,
                           SdfFieldKeys->FramesPerSecond);

        _GetTokenMetadata(metadata, _pseudoRoot->metadata,
                          UsdGeomTokens->upAxis);

        // Read the default prim name.
        _GetTokenMetadata(metadata, _pseudoRoot->metadata,
                          SdfFieldKeys->DefaultPrim);
    }

    // If no default prim then choose one by a heuristic (first root prim).
    if (!_pseudoRoot->children.empty()) {
        // Use emplace to leave existing property above untouched and avoid the
        // VtValue construction if possible (gcc-4.8 requires the fun syntax)
        _pseudoRoot->metadata.emplace(std::piecewise_construct,
            std::forward_as_tuple(SdfFieldKeys->DefaultPrim),
            std::forward_as_tuple(_pseudoRoot->children.front()));
    }

    return true;
}

void
_ReaderContext::Close()
{
    _Clear();

    _Lock lock(_mutex);
    _archive = IArchive();
    _mutex = NULL;
}

void
_ReaderContext::SetFlag(const TfToken& flagName, bool set)
{
    if (set) {
        _flags.insert(flagName);
    }
    else {
        _flags.erase(flagName);
    }
}

bool
_ReaderContext::IsFlagSet(const TfToken& flagName) const
{
    return _flags.count(flagName);
}

_ReaderContext::Prim&
_ReaderContext::AddPrim(const SdfPath& path)
{
    return _prims[path];
}

bool
_ReaderContext::IsInstance(const IObject& object) const
{
    // object is an instance if it's a key in _instances.
    return _instances.find(object.getFullName()) != _instances.end();
}

_ReaderContext::Prim&
_ReaderContext::AddInstance(const SdfPath& path, const IObject& object)
{
    // An instance is just a prim...
    Prim& result = AddPrim(path);

    // ...that also has its prototype member set.
    const auto i = _instances.find(object.getFullName());
    if (i != _instances.end()) {
        const _PrototypeInfo& info = _instanceSources.find(i->second)->second;
        result.prototype   = info.path;
        result.promoted = info.promoted;
    }
    return result;
}

_ReaderContext::Property&
_ReaderContext::FindOrCreateProperty(const SdfPath& path)
{
    return AddPrim(path.GetPrimPath()).propertiesCache[path.GetNameToken()];
}

const _ReaderContext::Property*
_ReaderContext::FindProperty(const SdfPath& path)
{
    _PrimMap::const_iterator i = _prims.find(path.GetPrimPath());
    if (i != _prims.end()) {
        PropertyMap::const_iterator j =
            i->second.propertiesCache.find(path.GetNameToken());
        if (j != i->second.propertiesCache.end()) {
            return &j->second;
        }
    }
    return NULL;
}

_ReaderContext::TimeSamples
_ReaderContext::ConvertSampleTimes(
    const _AlembicTimeSamples& alembicTimes) const
{
    std::vector<double> result;
    result.resize(alembicTimes.size());

    // Special case.
    if (_timeScale == 1.0 && _timeOffset == 0.0) {
        std::copy(alembicTimes.begin(), alembicTimes.end(), result.begin());
    }
    else {
        for (size_t i = 0, n = result.size(); i != n; ++i) {
            // Round the result so we get exact frames in the common
            // case of times stored in seconds and _timeScale = 1/24.
            static const double P = 1.0e+10;
            result[i] =
                GfRound(P * (alembicTimes[i] * _timeScale + _timeOffset)) / P;
        }
    }

    return TimeSamples(result);
}

void
_ReaderContext::AddSampleTimes(const TimeSamples& sampleTimes)
{
    sampleTimes.AddTo(&_allTimeSamples);
}

bool
_ReaderContext::HasSpec(const SdfPath& path) const
{
    if (const Prim* prim = _GetPrim(path)) {
        return path.IsAbsoluteRootOrPrimPath() || _GetProperty(*prim, path);
    }
    return false;
}

SdfSpecType
_ReaderContext::GetSpecType(const SdfPath& path) const
{
    if (const Prim* prim = _GetPrim(path)) {
        if (!path.IsAbsoluteRootOrPrimPath()) {
            if (_GetProperty(*prim, path)) {
                return SdfSpecTypeAttribute;
            }
        }
        else if (prim == _pseudoRoot) {
            return SdfSpecTypePseudoRoot;
        }
        else {
            return SdfSpecTypePrim;
        }
    }
    return SdfSpecTypeUnknown;
}

bool
_ReaderContext::HasField(
    const SdfPath& path,
    const TfToken& fieldName,
    const UsdAbc_AlembicDataAny& value) const
{
    TRACE_FUNCTION();

    if (const Prim* prim = _GetPrim(path)) {
        if (!path.IsAbsoluteRootOrPrimPath()) {
            if (const Property* property = _GetProperty(*prim, path)) {
                return _HasField(property, fieldName, value);
            }
        }
        else {
            return _HasField(prim, fieldName, value);
        }
    }
    return false;
}

bool
_ReaderContext::HasValue(
    const SdfPath& path,
    Index index,
    const UsdAbc_AlembicDataAny& value) const
{
    TRACE_FUNCTION();

    if (const Prim* prim = _GetPrim(path)) {
        if (!path.IsAbsoluteRootOrPrimPath()) {
            if (const Property* property = _GetProperty(*prim, path)) {
                return _HasValue(property, ISampleSelector(index), value);
            }
        }
    }
    return false;
}

void
_ReaderContext::VisitSpecs(
    const SdfAbstractData& owner,
    SdfAbstractDataSpecVisitor* visitor) const
{
    // Visit prims in path sorted order.
    for (const auto& v : _prims) {
        // Visit the prim.
        const SdfPath& primPath = v.first;
        if (!visitor->VisitSpec(owner, primPath)) {
            return;
        }

        // Visit the prim's properties in lexicographical sorted order.
        const Prim& prim = v.second;
        if (&prim != _pseudoRoot) {
            for (const auto& w : prim.propertiesCache) {
                if (!visitor->VisitSpec(owner,
                                        primPath.AppendProperty(w.first))) {
                    return;
                }
            }
        }
    }
}

TfTokenVector
_ReaderContext::List(const SdfPath& path) const
{
    TRACE_FUNCTION();

    TfTokenVector result;

    if (const Prim* prim = _GetPrim(path)) {
        if (!path.IsAbsoluteRootOrPrimPath()) {
            if (const Property* property = _GetProperty(*prim, path)) {
                result.push_back(SdfFieldKeys->TypeName);
                result.push_back(SdfFieldKeys->Custom);
                result.push_back(SdfFieldKeys->Variability);
                if (property->timeSampled) {
                    result.push_back(SdfFieldKeys->TimeSamples);
                }
                else if (!property->sampleTimes.IsEmpty()) {
                    result.push_back(SdfFieldKeys->Default);
                }

                // Add metadata.
                for (const auto& v : property->metadata) {
                    result.push_back(v.first);
                }
            }
        }
        else {
            if (prim != _pseudoRoot) {
                if (!prim->typeName.IsEmpty()) {
                    result.push_back(SdfFieldKeys->TypeName);
                }
                result.push_back(SdfFieldKeys->Specifier);
                if (!prim->properties.empty()) {
                    result.push_back(SdfChildrenKeys->PropertyChildren);
                }
                if (prim->primOrdering) {
                    result.push_back(SdfFieldKeys->PrimOrder);
                }
                if (prim->propertyOrdering) {
                    result.push_back(SdfFieldKeys->PropertyOrder);
                }
                if (!prim->prototype.IsEmpty()) {
                    result.push_back(SdfFieldKeys->References);
                }
                if (!prim->instanceSource.empty()) {
                    result.push_back(SdfFieldKeys->CustomData);
                }
                if (prim->instanceable && !prim->instanceSource.empty()) {
                    result.push_back(SdfFieldKeys->Instanceable);
                }
            }
            if (!prim->children.empty()) {
                result.push_back(SdfChildrenKeys->PrimChildren);
            }
            for (const auto& v : prim->metadata) {
                result.push_back(v.first);
            }
        }
    }

    return result;
}

const std::set<double>&
_ReaderContext::ListAllTimeSamples() const
{
    return _allTimeSamples;
}

const _ReaderContext::TimeSamples& 
_ReaderContext::ListTimeSamplesForPath(const SdfPath& path) const
{
    TRACE_FUNCTION();

    if (path.IsPropertyPath()) {
        if (const Prim* prim = _GetPrim(path)) {
            if (const Property* property = _GetProperty(*prim, path)) {
                if (property->timeSampled) {
                    return property->sampleTimes;
                }
            }
        }
    }

    static const TimeSamples empty;
    return empty;
}

void
_ReaderContext::_FindInstances(
    const IObject& parent,
    _SourceToInstancesMap* instances)
{
    for (size_t i = 0, n = parent.getNumChildren(); i != n; ++i) {
        IObject child(parent, parent.getChildHeader(i).getName());
        if (child.isInstanceRoot()) {
            // child is the top prim of an instance.
            (*instances)[child.getPtr()].insert(child.getInstancePtr());
        }
        else {
            // Descend the hierarchy outside of instance roots.  We can
            // terminate the recursion at the top of an instance's root
            // since there's nothing under there we won't see when we
            // traverse (or traversed) the instance source.
            _FindInstances(child, instances);
        }
    }
}

void
_ReaderContext::_FindPromotable(
    const _SourceToInstancesMap& instances,
    _ObjectReaderSet* promotable)
{
    // We want to use the parent of the source (and the parents of the
    // corresponging instances) where possible.  Since Usd can't share
    // the prototype prim but can share its descendants, we can get better
    // sharing when we can use the parent.  We can't do this if the
    // source or any instance has siblings and we won't do this unless
    // the source/instance is an IGeomBase and its parent is a transform.
    //
    // If we can use the parent we say the source is promotable.
    for (const auto& value: instances) {
        const _ObjectPtr& source = value.first;
        if (source->getParent()->getNumChildren() != 1) {
            // Source has siblings.
            continue;
        }

        // Source must be an IGeomBase and its parent must be an IXform.
        if (!IGeomBase::matches(source->getMetaData())) {
            continue;
        }
        if (!IXform::matches(source->getParent()->getMetaData())) {
            continue;
        }

        // Check instances for siblings.
        for (const auto& instance: value.second) {
            if (instance->getParent()->getNumChildren() != 1) {
                // Instance has siblings.
                continue;
            }
        }

        // We can promote this source.
        promotable->insert(source);
    }
}

void
_ReaderContext::_SetupInstancing(
    const _SourceToInstancesMap& instances,
    const _ObjectReaderSet& promotable,
    std::set<std::string>* usedNames)
{
    // Now build the mapping of instances to sources and a mapping from the
    // (possibly promoted) source full name to the corresponding Usd prototype
    // prim path.  We can no longer use Alembic to answer these questions
    // since it doesn't know about promoted prototypes/instances.
    for (const auto& value: instances) {
        const _ObjectPtr& source = value.first;
        const bool promoted = (promotable.find(source) != promotable.end());
        const std::string& sourceFullName = promoted
            ? source->getParent()->getFullName()
            : source->getFullName();
        if (promoted) {
            for (const auto& instance: value.second) {
                _instances[instance->getParent()->getFullName()] =
                    sourceFullName;
            }
        }
        else {
            // Not promotable.
            for (const auto& instance: value.second) {
                _instances[instance->getFullName()] = sourceFullName;
            }
        }

        // The Alembic instance source is just another instance as far
        // as Usd is concerned.  Unlike Alembic, Usd creates a separate
        // prototype that has special treatment.
        _instances[sourceFullName] = sourceFullName;

        // Construct a unique name.  Start by getting the name
        // portion of the instance source's path.
        const std::string::size_type j = sourceFullName.rfind('/');
        const std::string prototypeName =
            (j != std::string::npos)
                ? sourceFullName.substr(j + 1)
                : sourceFullName;

        // Now mangle/uniquify the name and make a root prim path.
        const SdfPath prototypePath =
                SdfPath::AbsoluteRootPath().AppendChild(TfToken(
                    _CleanName(prototypeName, " _", *usedNames,
                               _AlembicFixName(),
                               &SdfPath::IsValidIdentifier)));

        // Save the source/prototype info.
        _PrototypeInfo prototypeInfo = { prototypePath, promoted };
        _instanceSources.emplace(sourceFullName, std::move(prototypeInfo));
    }
}

void
_ReaderContext::_Clear()
{
    _prims.clear();
    _pseudoRoot = NULL;
    _allTimeSamples.clear();
    _instanceSources.clear();
    _instances.clear();
}

const _ReaderContext::Prim*
_ReaderContext::_GetPrim(const SdfPath& path) const
{
    _PrimMap::const_iterator i = _prims.find(path.GetAbsoluteRootOrPrimPath());
    return i == _prims.end() ? NULL : &i->second;
}

const _ReaderContext::Property*
_ReaderContext::_GetProperty(
    const Prim& prim,
    const SdfPath& path) const
{
    // The alembic reader does not support relational attributes; only prim
    // properties.
    if (!path.IsPrimPropertyPath()) {
        return nullptr;
    }
    PropertyMap::const_iterator i =
        prim.propertiesCache.find(path.GetNameToken());
    return i == prim.propertiesCache.end() ? NULL : &i->second;
}

bool
_ReaderContext::_HasField(
    const Prim* prim,
    const TfToken& fieldName,
    const UsdAbc_AlembicDataAny& value) const
{
    if (fieldName == SdfChildrenKeys->PrimChildren) {
        if (!prim->children.empty()) {
            return value.Set(prim->children);
        }
    }

    if (prim != _pseudoRoot) {
        if (fieldName == SdfFieldKeys->TypeName) {
            return value.Set(prim->typeName);
        }
        else if (fieldName == SdfFieldKeys->PrimOrder) {
            if (prim->primOrdering) {
                return value.Set(*prim->primOrdering);
            }
        }
        else if (fieldName == SdfFieldKeys->PropertyOrder) {
            if (prim->propertyOrdering) {
                return value.Set(*prim->propertyOrdering);
            }
        }
        else if (fieldName == SdfFieldKeys->Specifier) {
            return value.Set(prim->specifier);
        }
        else if (fieldName == SdfChildrenKeys->PropertyChildren) {
            if (!prim->properties.empty()) {
                return value.Set(prim->properties);
            }
        }
        else if (fieldName == SdfFieldKeys->CustomData) {
            // Provide the Alembic source path on prototype prims.  In Usd
            // we copy the instance source to a new root prototype prim and
            // the instance source becomes just another instance of the
            // prototype.  This gives us a breadcrumb to follow back.
            if (!prim->instanceSource.empty()) {
                static const std::string key("abcInstanceSourcePath");
                VtDictionary data;
                data[key] = VtValue(prim->instanceSource);
                return value.Set(data);
            }
        }
        else if (fieldName == SdfFieldKeys->Instanceable) {
            if (!prim->instanceSource.empty()) {
                return value.Set(prim->instanceable);
            }
        }
        else if (fieldName == SdfFieldKeys->References) {
            if (!prim->prototype.IsEmpty()) {
                SdfReferenceListOp refs;
                SdfReferenceVector items;
                items.push_back(SdfReference(std::string(), prim->prototype));
                refs.SetExplicitItems(items);
                return value.Set(refs);
            }
        }
    }

    TRACE_SCOPE("UsdAbc_AlembicDataReader::_HasField:OtherMetadata");
    MetadataMap::const_iterator j = prim->metadata.find(fieldName);
    if (j != prim->metadata.end()) {
        return value.Set(j->second);
    }

    return false;
}

bool
_ReaderContext::_HasField(
    const Property* property,
    const TfToken& fieldName,
    const UsdAbc_AlembicDataAny& value) const
{
    if (fieldName == SdfFieldKeys->Default) {
        // No default value if we're time sampled.  Alembic does not
        // distinguish default and time samples so we either have one
        // sample (the default) or more than one sample (time sampled).
        if (!property->timeSampled && !property->sampleTimes.IsEmpty()){
            return _HasValue(property, ISampleSelector(), value);
        }
    }
    else if (fieldName == SdfFieldKeys->TimeSamples) {
        if (property->timeSampled) {
            if (value) {
                TRACE_SCOPE("UsdAbc_AlembicDataReader::_HasField:TimeSamples");
                // Fill a map of values over all time samples.
                VtValue tmp;
                UsdAbc_AlembicDataAny any(&tmp);
                SdfTimeSampleMap samples;
                const Index n = property->sampleTimes.GetSize();
                for (Index j = 0; j != n; ++j) {
                    if (_HasValue(property, ISampleSelector(j), any)) {
                        samples[property->sampleTimes[j]] = tmp;
                    }
                }
                return value.Set(samples);
            }
            else {
                return true;
            }
        }
    }
    else if (fieldName == SdfFieldKeys->TypeName) {
        return value.Set(property->typeName.GetAsToken());
    }
    else if (fieldName == SdfFieldKeys->Variability) {
        return value.Set(property->uniform ? 
                         SdfVariabilityUniform : SdfVariabilityVarying);
    }
    
    TRACE_SCOPE("UsdAbc_AlembicDataReader::_HasField:OtherMetadata");
    MetadataMap::const_iterator j = property->metadata.find(fieldName);
    if (j != property->metadata.end()) {
        return value.Set(j->second);
    }

    return false;
}

bool
_ReaderContext::_HasValue(
    const Property* property,
    const ISampleSelector& selector,
    const UsdAbc_AlembicDataAny& value) const
{
    TRACE_FUNCTION();

    // Check if no conversion available.
    if (!property->converter) {
        return false;
    }

    // See if only checking for existence.
    if (value.IsEmpty()) {
        return true;
    }

    TRACE_SCOPE("UsdAbc_AlembicDataReader::_HasValue:Conversion");
    _Lock lock(_mutex);
    return property->converter(value, selector);
}

//
// Utilities
//

/// Return the number of interesting samples in an object
template <typename T> size_t
_GetNumSamples(const T& object) {
    size_t nSamples = object.getNumSamples();
    if (!object.isConstant())
        return nSamples;

    return std::min(nSamples, size_t(1));
}

/// Fill sample times from an object with getTimeSampling() and
/// getNumSamples() methods.
template <class T>
static
_AlembicTimeSamples
_GetSampleTimes(const T& object)
{
    _AlembicTimeSamples result;
    if (object.valid()) {
        TimeSamplingPtr timeSampling = object.getTimeSampling();
        for (size_t i = 0, n = _GetNumSamples(object); i < n; ++i) {
            result.push_back(timeSampling->getSampleTime(i));
        }
    }
    return result;
}

template <class T>
static
_AlembicTimeSamples
_GetSampleTimes(const ISchemaObject<T>& object)
{
    return _GetSampleTimes(object.getSchema());
}

static
TfToken
_GetRole(const std::string& role)
{
    if (role.empty()) {
        return TfToken();
    }
    if (role == "point") {
        return SdfValueRoleNames->Point;
    }
    if (role == "normal") {
        return SdfValueRoleNames->Normal;
    }
    if (role == "vector") {
        return SdfValueRoleNames->Vector;
    }
    if (role == "rgb") {
        return SdfValueRoleNames->Color;
    }
    if (role == "point") {
        return SdfValueRoleNames->Point;
    }
    if (role == "rgba") {
        // No Usd types for RGBA colors.
        return TfToken();
    }
    if (role == "matrix") {
        // No special treatment.
        return TfToken();
    }
    if (role == "quat") {
        // Special case.
        return TfToken("quat");
    }
    // Unknown.
    return TfToken();
}

static
SdfValueTypeName
_GetInterpretation(const SdfValueTypeName& typeName, const TfToken& role)
{
    if (role == "quat") {
        if (typeName == SdfValueTypeNames->Float4) {
            return SdfValueTypeNames->Quatf;
        } 
        if (typeName == SdfValueTypeNames->Double4) {
            return SdfValueTypeNames->Quatd;
        }
    }

    // Get the type for the role, if any, otherwise use the input type.
    // Using the input type as a fallback will, among other things,
    // convert a float[2] with a "vector" interpretation to Float2;
    // note that there is no Vector2f in Usd so the FindType() would
    // yield an empty type in this case.
    SdfValueTypeName result =
        SdfSchema::GetInstance().FindType(typeName.GetType(), role);
    return result ? result : typeName;
}

static
TfToken
_GetInterpolation(GeometryScope geometryScope)
{
    static const TfToken constant("constant");
    static const TfToken uniform("uniform");
    static const TfToken varying("varying");
    static const TfToken vertex("vertex");
    static const TfToken faceVarying("faceVarying");
    switch (geometryScope) {
    case kConstantScope: return constant;
    case kUniformScope: return uniform;
    case kVaryingScope: return varying;
    case kVertexScope: return vertex;
    case kFacevaryingScope: return faceVarying;
    default: return TfToken();
    }
}

//
// _PrimReaderContext
//

struct _IsValidTag { };
struct _MetaDataTag { };
struct _SampleTimesTag { };

/// \class _PrimReaderContext
/// \brief The Usd to Alembic prim reader context.
class _PrimReaderContext {
public:
    typedef _ReaderContext::Converter Converter;
    typedef _ReaderContext::Prim Prim;
    typedef _ReaderContext::Property Property;
    typedef _IsValidTag IsValidTag;
    typedef _MetaDataTag MetaDataTag;
    typedef _SampleTimesTag SampleTimesTag;
    typedef std::function<bool (IsValidTag)> IsValid;
    typedef std::function<const MetaData& (MetaDataTag)> GetMetaData;
    typedef std::function<_AlembicTimeSamples(SampleTimesTag)> GetSampleTimes;

    _PrimReaderContext(_ReaderContext&,
                       const IObject& prim,
                       const SdfPath& path);

    /// Returns the prim object.
    IObject GetObject() const;

    /// Returns the Usd path to this prim.
    const SdfPath& GetPath() const;

    /// Returns \p name converted to a valid Usd name not currently used
    /// by any property on this prim.
    std::string GetUsdName(const std::string& name) const;

    /// Returns the prim cache.
    Prim& GetPrim();

    /// Adds a property named \p name of type \p typeName with the converter
    /// \p converter.  \p converter must be a functor object that conforms
    /// to the \c IsValid, \c Converter, \c GetSampleTimes and \c GetMetaData
    /// types.  If \p convert is invalid then this does nothing.
    template <class T>
    void AddProperty(const TfToken& name, const SdfValueTypeName& typeName,
                     const T& converter);

    /// Adds a uniform property named \p name of type \p typeName with the
    /// converter \p converter.  \p converter must be a functor object that
    /// conforms to the \c IsValid, \c Converter, \c GetSampleTimes and
    /// \c GetMetaData types.  If \p convert is invalid then this does nothing.
    template <class T>
    void AddUniformProperty(const TfToken& name, 
                            const SdfValueTypeName& typeName,
                            const T& converter);

    /// Add an out-of-schema property, which uses the default conversion
    /// for whatever Alembic type the property is.  If \p property is a
    /// compound property then all of its descendants are added as
    /// out-of-schema properties, building a namespaced name from the
    /// property hierarchy with \p name as the left-most component.
    void AddOutOfSchemaProperty(const std::string& name,
                                const AlembicProperty& property);

    /// Set the schema.  This makes additional properties available via
    /// the \c ExtractSchema() method.
    void SetSchema(const std::string& schemaName);

    /// Get a sample from the schema.  This doesn't extract any properties.
    /// If the given type doesn't match the schema then this returns false
    /// otherwise it returns true.
    template <typename T>
    bool GetSample(typename T::schema_type::Sample& sample,
                   const ISampleSelector& iss) const
    {
        typename T::schema_type schema(_prim.getProperties());
        if (schema.valid()) {
            schema.get(sample, iss);
            return true;
        }
        return false;
    }

    /// Returns the alembic property named \p name.  If the property doesn't
    /// exist or has already been extracted then this returns an empty
    /// property object.  The property is extracted from the context so
    /// it cannot be extracted again.
    AlembicProperty Extract(const std::string& name);

    /// Returns the alembic property named \p name in the schema. If
    /// the property doesn't exist or has already been extracted then
    /// this returns an empty property object.  The property is extracted
    /// from the context so it cannot be extracted again.
    AlembicProperty ExtractSchema(const std::string& name);

    /// Returns the names of properties that have not been extracted yet
    /// in Alembic property order.
    std::vector<std::string> GetUnextractedNames() const;

    /// Returns a _PrimReaderContext corresponding to the parent of this context.
    _PrimReaderContext GetParentContext() const;

private:
    Property& _AddProperty(const TfToken& name);
    Property& _AddProperty(const TfToken& name,
                           const SdfValueTypeName& typeName,
                           const GetMetaData& metadata,
                           const GetSampleTimes& sampleTimes,
                           bool isOutOfSchemaProperty);
    Property& _AddProperty(const TfToken& name,
                           const SdfValueTypeName& typeName,
                           const MetaData& metadata,
                           const _AlembicTimeSamples& sampleTimes,
                           bool isOutOfSchemaProperty);
    void _GetPropertyMetadata(const MetaData& alembicMetadata,
                              Property* property,
                              bool isOutOfSchemaProperty) const;

private:
    _ReaderContext& _context;
    IObject _prim;
    ICompoundProperty _schema;
    SdfPath _path;
    std::vector<std::string> _unextracted;
    std::vector<std::string> _unextractedSchema;
    std::set<std::string> _usedNames;
};

_PrimReaderContext::_PrimReaderContext(
    _ReaderContext& context,
    const IObject& prim,
    const SdfPath& path) :
    _context(context),
    _prim(prim),
    _path(path)
{
    // Fill _unextracted with all of the property names.
    ICompoundProperty properties = _prim.getProperties();
    _unextracted.resize(properties.getNumProperties());
    for (size_t i = 0, n = _unextracted.size(); i != n; ++i) {
        _unextracted[i] = properties.getPropertyHeader(i).getName();
    }

    // Pre-populate _usedNames with the names as-is.
    _usedNames.insert(_unextracted.begin(), _unextracted.end());
}

IObject
_PrimReaderContext::GetObject() const
{
    return _prim;
}

const SdfPath&
_PrimReaderContext::GetPath() const
{
    return _path;
}

std::string
_PrimReaderContext::GetUsdName(const std::string& name) const
{
    return _CleanName(name, " .", _usedNames,
                      _AlembicFixNamespacedName(),
                      &SdfPath::IsValidNamespacedIdentifier);
}

_PrimReaderContext::Prim&
_PrimReaderContext::GetPrim()
{
    return _context.AddPrim(GetPath());
}

void
_PrimReaderContext::SetSchema(const std::string& schemaName)
{
    _unextractedSchema.clear();
    _schema = ICompoundProperty(_prim.getProperties(), schemaName,
                                ErrorHandler::kQuietNoopPolicy);
    if (_schema.valid()) {
        // Fill _unextractedSchema with all of the property names.
        _unextractedSchema.resize(_schema.getNumProperties());
        for (size_t i = 0, n = _unextractedSchema.size(); i != n; ++i) {
            _unextractedSchema[i] = _schema.getPropertyHeader(i).getName();
        }
    }

    // Pre-populate _usedNames with the names as-is.
    _usedNames.insert(_unextractedSchema.begin(), _unextractedSchema.end());
}

AlembicProperty
_PrimReaderContext::Extract(const std::string& name)
{
    std::vector<std::string>::iterator i =
        std::find(_unextracted.begin(), _unextracted.end(), name);
    if (i != _unextracted.end()) {
        _unextracted.erase(i);
        return AlembicProperty(GetPath(), name, GetObject());
    }
    return AlembicProperty(GetPath(), name);
}

AlembicProperty
_PrimReaderContext::ExtractSchema(const std::string& name)
{
    std::vector<std::string>::iterator i =
        std::find(_unextractedSchema.begin(), _unextractedSchema.end(), name);
    if (i != _unextractedSchema.end()) {
        _unextractedSchema.erase(i);
        return AlembicProperty(GetPath(), name, _schema);
    }
    return AlembicProperty(GetPath(), name);
}

std::vector<std::string>
_PrimReaderContext::GetUnextractedNames() const
{
    return _unextracted;
}

template <class T>
void
_PrimReaderContext::AddProperty(
    const TfToken& name,
    const SdfValueTypeName& typeName,
    const T& converter)
{
    if (converter(IsValidTag())) {
        _AddProperty(name, typeName, converter, converter, false).converter=converter;
    }
}

template <class T>
void
_PrimReaderContext::AddUniformProperty(
    const TfToken& name,
    const SdfValueTypeName& typeName,
    const T& converter)
{
    if (converter(IsValidTag())) {
        Property &prop = _AddProperty(name, typeName, converter, converter, false);
        prop.converter=converter;
        prop.uniform = true;
        prop.timeSampled = false;
    }
}

void
_PrimReaderContext::AddOutOfSchemaProperty(
    const std::string& name,
    const AlembicProperty& property)
{
    // Get property info.
    const PropertyHeader* header = property.GetHeader();
    if (!header) {
        // No such property.
        return;
    }

    // Handle compound properties.
    if (header->isCompound()) {
        ICompoundProperty compound = property.Cast<ICompoundProperty>();

        // Fill usedNames.
        std::set<std::string> usedNames;
        for (size_t i = 0, n = compound.getNumProperties(); i != n; ++i) {
            usedNames.insert(compound.getPropertyHeader(i).getName());
        }

        // Recurse.
        for (size_t i = 0, n = compound.getNumProperties(); i != n; ++i) {
            const std::string rawName =
                compound.getPropertyHeader(i).getName();
            const std::string cleanName =
                _CleanName(rawName, " .", usedNames,
                           _AlembicFixName(),
                           &SdfPath::IsValidIdentifier);
            const std::string namespacedName =
                SdfPath::JoinIdentifier(name, cleanName);
            const SdfPath childPath =
                GetPath().AppendProperty(TfToken(namespacedName));
            AddOutOfSchemaProperty(
                namespacedName,
                AlembicProperty(childPath, rawName, compound));
        }

        return;
    }

    // Get the sample times.
    _AlembicTimeSamples sampleTimes;
    if (header->isScalar()) {
        sampleTimes = _GetSampleTimes(property.Cast<IScalarProperty>());
    }
    else {
        sampleTimes = _GetSampleTimes(property.Cast<IArrayProperty>());
    }

    // Get the converter and add the property.
    const bool isOutOfSchema = true;
    const UsdAbc_AlembicType alembicType(*header);
    SdfValueTypeName usdTypeName =
        _context.GetSchema().GetConversions().FindConverter(alembicType);

    if (usdTypeName) {
        TfToken propName(name);
        if (TfGetEnvSetting(USD_ABC_WRITE_UV_AS_ST_TEXCOORD2FARRAY) &&
            name == UsdAbcPropertyNames->uvIndices) {
            propName = UsdAbcPropertyNames->stIndices;
        }
        else if (TfStringEndsWith(name, _tokens->indicesSuffix)){
            // If the property is indexed and we're adding the indices here
            // then we want to make sure they are added as an IntArray
            // as that's the only supported type for index arrays.
            usdTypeName = SdfValueTypeNames->IntArray;
        }
        else if (TfStringEndsWith(name, _tokens->valuesSuffix)) {
            // If the property is indexed and we're adding the values
            // it might have a ":vals" suffix. We need to remove it
            // to be compatible.
            propName = TfToken(name.substr(0, name.size() - _tokens->valuesSuffix.size()));
        }

        _PrimReaderContext::Property &prop = _AddProperty(
            propName,
            usdTypeName,
            header->getMetaData(),
            sampleTimes,
            isOutOfSchema
        );

        prop.converter = std::bind(
            _context.GetSchema().GetConversions().GetToUsdConverter(
                alembicType, prop.typeName),
            property.GetParent(), property.GetName(),
            std::placeholders::_2, std::placeholders::_1);
    }
    else {
        TF_WARN("No conversion for \"%s\" of type \"%s\" at <%s>",
                name.c_str(),
                alembicType.Stringify().c_str(),
                GetPath().GetText());
    }
}

_PrimReaderContext::Property&
_PrimReaderContext::_AddProperty(const TfToken& name)
{
    // Create the property cache and add the property to the prim's
    // properties.
    const SdfPath path = GetPath().AppendProperty(name);
    if (_context.FindProperty(path)) {
        // Existing property.  We allow duplicate properties in order to
        // support multiple Alembic sources (particularly for color).
    }
    else {
        // New property.
        GetPrim().properties.push_back(name);
        _usedNames.insert(name);
    }
    return _context.FindOrCreateProperty(path);
}

_PrimReaderContext::Property&
_PrimReaderContext::_AddProperty(
    const TfToken& name,
    const SdfValueTypeName& typeName,
    const GetMetaData& metadata,
    const GetSampleTimes& sampleTimes,
    bool isOutOfSchemaProperty)
{
    return _AddProperty(name, typeName,
                        metadata(MetaDataTag()),
                        sampleTimes(SampleTimesTag()),
                        isOutOfSchemaProperty);
}

_PrimReaderContext::Property&
_PrimReaderContext::_AddProperty(
    const TfToken& name,
    const SdfValueTypeName& typeName,
    const MetaData& metadata,
    const _AlembicTimeSamples& sampleTimes,
    bool isOutOfSchemaProperty)
{
    // Make the property.
    Property& property = _AddProperty(name);

    // Save the Usd type.
    property.typeName = typeName;

    // Save the time sampling.
    property.sampleTimes = _context.ConvertSampleTimes(sampleTimes);
    property.timeSampled = (property.sampleTimes.GetSize() > 0);

    // Save metadata.  This may change the value of 'timeSampled'
    _GetPropertyMetadata(metadata, &property, isOutOfSchemaProperty);

    if (property.timeSampled) {
        _context.AddSampleTimes(property.sampleTimes);
    }

#ifdef USDABC_ALEMBIC_DEBUG
    fprintf(stdout, "%*s%s%s %s\n",
            2 * ((int)GetPath().GetPathElementCount() + 1), "",
            property.metadata[SdfFieldKeys->Custom].UncheckedGet<bool>()
                ? "custom " : "",
            property.typeName.GetAsToken().GetText(), name.GetText());
#endif

    return property;
}

void
_PrimReaderContext::_GetPropertyMetadata(
    const MetaData& alembicMetadata,
    Property* property,
    bool isOutOfSchemaProperty) const
{
    std::string value;
    MetadataMap& usdMetadata = property->metadata;

    // Custom.  This is required metadata.
    usdMetadata[SdfFieldKeys->Custom] = VtValue(isOutOfSchemaProperty);
    _GetBoolMetadata(alembicMetadata, usdMetadata, SdfFieldKeys->Custom);

    // Variability.  This is required metadata.
    if (alembicMetadata.get(_AmdName(SdfFieldKeys->Variability)) == "uniform") {
        usdMetadata[SdfFieldKeys->Variability] = SdfVariabilityUniform;
    }
    else {
        usdMetadata[SdfFieldKeys->Variability] = SdfVariabilityVarying;
    }

    // Type name.
    if (!property->typeName) {
        property->typeName =
            SdfSchema::GetInstance().FindType(
                alembicMetadata.get(_AmdName(SdfFieldKeys->TypeName)));
    }

    // If there's only one timeSample and we've indicated it should be 
    // converted into Default, disable timeSampling.
    if (property->sampleTimes.GetSize() == 1 && 
        alembicMetadata.get(_AmdName(UsdAbcCustomMetadata->singleSampleAsDefault)) == "true") {
        property->timeSampled = false;
    }

    // Adjust the type name by the interpretation.
    property->typeName =
        _GetInterpretation(property->typeName,
                           _GetRole(alembicMetadata.get("interpretation")));

    // Set the interpolation if there is one.
    if (!alembicMetadata.get("geoScope").empty()) {
        const TfToken interpolation =
            _GetInterpolation(GetGeometryScope(alembicMetadata));
        if (!interpolation.IsEmpty()) {
            usdMetadata[UsdGeomTokens->interpolation] = VtValue(interpolation);
        }
    }

    // Other Sdf metadata.
    _GetStringMetadata(alembicMetadata, usdMetadata, SdfFieldKeys->DisplayGroup);
    _GetStringMetadata(alembicMetadata, usdMetadata, SdfFieldKeys->Documentation);
    _GetBoolMetadata(alembicMetadata, usdMetadata, SdfFieldKeys->Hidden);

    // Custom metadata.
    _GetStringMetadata(alembicMetadata, usdMetadata, 
                       UsdAbcCustomMetadata->riName);
    _GetStringMetadata(alembicMetadata, usdMetadata, 
                       UsdAbcCustomMetadata->riType);
    _GetBoolMetadata(alembicMetadata, usdMetadata,
                     UsdAbcCustomMetadata->gprimDataRender);
}

_PrimReaderContext 
_PrimReaderContext::GetParentContext() const
{    
    _PrimReaderContext parentContext(
        _context, 
        _prim.getParent(), 
        _path.GetParentPath());
    return parentContext;
}

//
// Copy functors
//

/// Convert a scalar property value to a VtValue.
template <class AlembicTraits, class UsdType, class AlembicType>
static
VtValue
_CopyGenericValue(const AlembicType& src)
{
    typedef typename PODTraitsFromEnum<
        AlembicTraits::pod_enum>::value_type SrcType;
    static const int SrcExtent = AlembicTraits::extent;

    // All Alembic data is packed POD elements so we can simply use the
    // address of src as the address of the first POD element.
    const SrcType* srcData = reinterpret_cast<const SrcType*>(&src);

    // Convert.
    return VtValue(_ConvertPODToUsd<UsdType, SrcType, SrcExtent>()(*srcData));
}

/// Convert an array property value to a VtValue.
template <class AlembicTraits, class UsdType>
static
VtValue
_CopyGenericValue(const shared_ptr<TypedArraySample<AlembicTraits> >& src)
{
    typedef typename PODTraitsFromEnum<
        AlembicTraits::pod_enum>::value_type SrcType;
    static const int SrcExtent = AlembicTraits::extent;

    // All Alembic data is packed POD elements so we can simply use the
    // address of src as the address of the first POD element.
    const SrcType* srcData = reinterpret_cast<const SrcType*>(src->get());

    // Convert.
    VtArray<UsdType> result(src->size());
    _ConvertPODToUsdArray<UsdType, SrcType, SrcExtent>()(result.data(),
                                                         srcData, src->size());
    return VtValue(result);
}

/// Return a constant (default) value.
struct _CopySynthetic {
    VtValue value;
    MetaData metadata;
    template <class T>
    _CopySynthetic(const T& value_) : value(value_) { }

    bool operator()(_IsValidTag) const
    {
        return true;
    }

    const MetaData& operator()(_MetaDataTag) const
    {
        return metadata;
    }

    _AlembicTimeSamples operator()(_SampleTimesTag) const
    {
        return _AlembicTimeSamples(1, 0.0);
    }

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        return dst.Set(value);
    }
};

/// Copy a value from a property of a given type to \c UsdValueType.
template <class T, class UsdValueType = void, bool expand = true>
struct _CopyGeneric {
    typedef T PropertyType;
    // This is defined for ITyped*Property.
    typedef typename PropertyType::traits_type AlembicTraits;

    PropertyType object;
    _CopyGeneric(const AlembicProperty& object_,
                 SchemaInterpMatching matching = kStrictMatching) :
        object(object_.Cast<PropertyType>(matching)) { }

    bool operator()(_IsValidTag) const
    {
        return object.valid();
    }

    const MetaData& operator()(_MetaDataTag) const
    {
        return object.getMetaData();
    }

    _AlembicTimeSamples operator()(_SampleTimesTag) const
    {
        return _GetSampleTimes(object);
    }

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        return dst.Set(_CopyGenericValue<AlembicTraits,
                                         UsdValueType>(object.getValue(iss)));
    }
};

/// Copy a ITypedGeomParam.  These are either an ITypedArrayProperty or a
/// compound property with an ITypedArrayProperty and indices. If the template
/// parameter `expand` is true (which is the default), then we will call
/// getExpanced() to get the un-indexed values. Otherwise we will get the
/// indexed values, but _CopyIndices will need to be used to extract the
/// indices.
template <class T, class UsdValueType, bool expand>
struct _CopyGeneric<ITypedGeomParam<T>, UsdValueType, expand> {
    typedef ITypedGeomParam<T> GeomParamType;
    typedef typename GeomParamType::prop_type PropertyType;
    typedef typename PropertyType::traits_type AlembicTraits;

    GeomParamType object;
    _CopyGeneric(const AlembicProperty& object_) :
        object(object_.Cast<GeomParamType>()) { }

    bool operator()(_IsValidTag) const
    {
        return object.valid();
    }

    const MetaData& operator()(_MetaDataTag) const
    {
        return object.getMetaData();
    }

    _AlembicTimeSamples operator()(_SampleTimesTag) const
    {
        return _GetSampleTimes(object);
    }

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        typename GeomParamType::sample_type sample;
        if (expand == false && object.isIndexed()) {
            object.getIndexed(sample, iss);
        } else {
            object.getExpanded(sample, iss);
        }
        return dst.Set(_CopyGenericValue<AlembicTraits,
                                         UsdValueType>(sample.getVals()));
    }
};

/// Copy a ITypedGeomParam's index list as an int array.
/// If the Alembic property is not indexed, it will do nothing.
template <class GeomParamType>
struct _CopyIndices {
    typedef typename GeomParamType::prop_type PropertyType;
    typedef typename PropertyType::traits_type AlembicTraits;

    GeomParamType object;
    _CopyIndices(const AlembicProperty& object_) :
        object(object_.Cast<GeomParamType>())
    {
    }

    bool operator()(_IsValidTag) const
    {
        return object.valid();
    }

    const MetaData& operator()(_MetaDataTag) const
    {
        return object.getMetaData();
    }

    _AlembicTimeSamples operator()(_SampleTimesTag) const
    {
        return _GetSampleTimes(object);
    }

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        if (object.isIndexed()) {
            typename GeomParamType::sample_type sample;
            object.getIndexed(sample, iss);
            return dst.Set(_CopyGenericValue<Uint32TPTraits,
                                             int>(sample.getIndices()));
        }
        return false;
    }
};

/// Copy hermite points from interleaved IP3fArrayProperty.
struct _CopyHermitePoints : _CopyGeneric<IP3fArrayProperty, GfVec3f> {
    _CopyHermitePoints(const AlembicProperty& object_)
        : _CopyGeneric<IP3fArrayProperty, GfVec3f>(object_, kStrictMatching) {}

    using _CopyGeneric<IP3fArrayProperty, GfVec3f>::operator();

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const {
        typedef typename IP3fArrayProperty::sample_ptr_type SamplePtr;
        typedef typename IP3fArrayProperty::traits_type AlembicTraits;
        SamplePtr samplePtr;
        object.get(samplePtr, iss);
        VtValue value = _CopyGenericValue<AlembicTraits, GfVec3f>(samplePtr);
        VtVec3fArray interleaved = value.Get<VtVec3fArray>();
        auto separated =
            UsdGeomHermiteCurves::PointAndTangentArrays::Separate(interleaved);
        return dst.Set(VtValue(separated.GetPoints()));
    }
};

/// Copy hermite points from interleaved IP3fArrayProperty.
struct _CopyHermiteTangents : _CopyGeneric<IP3fArrayProperty, GfVec3f> {
    _CopyHermiteTangents(const AlembicProperty& object_)
        : _CopyGeneric<IP3fArrayProperty, GfVec3f>(object_, kStrictMatching) {}

    using _CopyGeneric<IP3fArrayProperty, GfVec3f>::operator();

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const {
        typedef typename IP3fArrayProperty::sample_ptr_type SamplePtr;
        typedef typename IP3fArrayProperty::traits_type AlembicTraits;
        SamplePtr samplePtr;
        object.get(samplePtr, iss);
        VtValue value = _CopyGenericValue<AlembicTraits, GfVec3f>(samplePtr);
        VtVec3fArray interleaved = value.Get<VtVec3fArray>();
        auto separated =
            UsdGeomHermiteCurves::PointAndTangentArrays::Separate(interleaved);
        return dst.Set(VtValue(separated.GetTangents()));
    }
};

/// Copy a bounding box from an IBox3dProperty.
struct _CopyBoundingBox : _CopyGeneric<IBox3dProperty> {
    _CopyBoundingBox(const AlembicProperty& object_) :
        _CopyGeneric<IBox3dProperty>(object_) { }

    using _CopyGeneric<IBox3dProperty>::operator();

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        const Box3d box = object.getValue(iss);
        const double* p = reinterpret_cast<const double*>(&box);
        VtArray<GfVec3f> result(2);
        result[0] = GfVec3f(p[0], p[1], p[2]);
        result[1] = GfVec3f(p[3], p[4], p[5]);
        return dst.Set(result);
    }
};

/// Copy a orientation from an IStringProperty.
struct _CopyOrientation : _CopyGeneric<IStringProperty> {
    _CopyOrientation(const AlembicProperty& object_) :
        _CopyGeneric<IStringProperty>(object_) { }

    using _CopyGeneric<IStringProperty>::operator();

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        return dst.Set(TfToken(object.getValue(iss)));
    }
};

/// Copy a visibility from an ICharProperty.
struct _CopyVisibility : _CopyGeneric<ICharProperty> {
    _CopyVisibility(const AlembicProperty& object_) :
        _CopyGeneric<ICharProperty>(object_) { }

    using _CopyGeneric<ICharProperty>::operator();

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        const ObjectVisibility vis = 
            static_cast<ObjectVisibility>(object.getValue(iss));
        switch (vis) {
        case kVisibilityHidden:
            return dst.Set(UsdGeomTokens->invisible);

        default:
        case kVisibilityVisible:
        {
            const std::string authoredValue = 
                (vis == kVisibilityVisible ? 
                 "kVisibilityVisible" : TfStringify(vis));
            // Usd doesn't support this.
            _PostUnsupportedValueWarning(
                object, iss, WarningVisibility, 
                authoredValue.c_str(), "kVisibilityDeferred");
            // Fall through
        }
        case kVisibilityDeferred:
            return dst.Set(UsdGeomTokens->inherited);
        }
    }
};

/// Copy a color from Maya export.
struct _CopyAdskColor : _CopyGeneric<IC4fProperty> {
    _CopyAdskColor(const AlembicProperty& object_) :
        _CopyGeneric<IC4fProperty>(object_) { }

    using _CopyGeneric<IC4fProperty>::operator();

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        const C4f color = object.getValue(iss);
        VtArray<GfVec3f> result(1);
        result[0] = GfVec3f(color[0], color[1], color[2]);
        return dst.Set(result);
    }
};

/// Copy a Transform from an IXform.
struct _CopyXform {
    IXform object;
    _CopyXform(const IXform& object_) : object(object_) { }

    bool operator()(_IsValidTag) const
    {
        return object.valid();
    }

    const MetaData& operator()(_MetaDataTag) const
    {
        // Extract any metadata provided by the Usd Alembic writer.
        if (!_metadata) {
            _metadata = MetaData();
            const MetaData& srcMetadata = object.getMetaData();
            for (MetaData::const_iterator i  = srcMetadata.begin();
                                          i != srcMetadata.end(); ++i) {
                if (!i->second.empty() && 
                        i->first.compare(0, 14, "Usd.transform:") == 0) {
                    _metadata->set(i->first.substr(14), i->second);
                }
            }
        }
        return *_metadata;
    }

    _AlembicTimeSamples operator()(_SampleTimesTag) const
    {
        return _GetSampleTimes(object);
    }

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        M44d matrix = object.getSchema().getValue(iss).getMatrix();
        return dst.Set(GfMatrix4d(matrix.x));
    }

private:
    mutable boost::optional<MetaData> _metadata;
};

/// Base class to copy attributes of an almebic camera to a USD camera
struct _CopyCameraBase {
    ICamera object;

    _CopyCameraBase(const ICamera& object_) : object(object_) { }

    bool operator()(_IsValidTag) const
    {
        return object.valid();
    }

    const MetaData& operator()(_MetaDataTag) const
    {
        return object.getMetaData();
    }

    _AlembicTimeSamples operator()(_SampleTimesTag) const
    {
        return _GetSampleTimes(object);
    }
};

/// Class to copy focal length
struct _CopyCameraFocalLength : _CopyCameraBase {
    using _CopyCameraBase::operator();

    _CopyCameraFocalLength(const ICamera& object_) 
        : _CopyCameraBase(object_) {};

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        // Focal Length is just copied into USD.
        // Both use mm as unit.
        const CameraSample sample = object.getSchema().getValue(iss);
        const float focalLength = sample.getFocalLength();
        return dst.Set(focalLength);
    }
};

/// Class to copy horizontal aperture
struct _CopyCameraHorizontalAperture : _CopyCameraBase {
    using _CopyCameraBase::operator();

    _CopyCameraHorizontalAperture(const ICamera& object_) 
        : _CopyCameraBase(object_) {};

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        const CameraSample sample = object.getSchema().getValue(iss);

        // USD uses mm, alembic uses cm

        const float horizontalAperture =
            sample.getHorizontalAperture() *
            sample.getLensSqueezeRatio() * 10.0;

        return dst.Set(horizontalAperture);
    }
};

/// Class to copy vertical aperture
struct _CopyCameraVerticalAperture : _CopyCameraBase {
    using _CopyCameraBase::operator();

    _CopyCameraVerticalAperture(const ICamera& object_) 
        : _CopyCameraBase(object_) {};

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        const CameraSample sample = object.getSchema().getValue(iss);

        // USD uses mm, alembic uses cm

        const float verticalAperture =
            sample.getVerticalAperture() *
            sample.getLensSqueezeRatio() * 10.0;

        return dst.Set(verticalAperture);
    }
};

/// Class to copy horizontal aperture offset
struct _CopyCameraHorizontalApertureOffset : _CopyCameraBase {
    using _CopyCameraBase::operator();

    _CopyCameraHorizontalApertureOffset(const ICamera& object_) 
        : _CopyCameraBase(object_) {};

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        const CameraSample sample = object.getSchema().getValue(iss);

        // USD uses mm, alembic uses cm

        const float horizontalApertureOffset =
            sample.getHorizontalFilmOffset() *
            sample.getLensSqueezeRatio() * 10.0;

        return dst.Set(horizontalApertureOffset);
    }
};

/// Class to copy vertical aperture offset
struct _CopyCameraVerticalApertureOffset : _CopyCameraBase {
    using _CopyCameraBase::operator();

    _CopyCameraVerticalApertureOffset(const ICamera& object_) 
        : _CopyCameraBase(object_) {};

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        const CameraSample sample = object.getSchema().getValue(iss);

        // USD uses mm, alembic uses cm

        const float verticalApertureOffset =
            sample.getVerticalFilmOffset() *
            sample.getLensSqueezeRatio() * 10.0;
        return dst.Set(verticalApertureOffset);
    }
};

/// Class to copy clippingRange
struct _CopyCameraClippingRange : _CopyCameraBase {
    using _CopyCameraBase::operator();

    _CopyCameraClippingRange(const ICamera& object_)
        : _CopyCameraBase(object_) {};

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        const CameraSample sample = object.getSchema().getValue(iss);

        return dst.Set(GfVec2f(sample.getNearClippingPlane(), 
                               sample.getFarClippingPlane()));
    }
};

/// Copy a subdivision scheme from an IStringProperty.
struct _CopySubdivisionScheme : _CopyGeneric<IStringProperty> {
    _CopySubdivisionScheme(const AlembicProperty& object_) :
        _CopyGeneric<IStringProperty>(object_) { }

    using _CopyGeneric<IStringProperty>::operator();

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        const std::string value = object.getValue(iss);
        if (value.empty() || value == "catmull-clark") {
            return dst.Set(UsdGeomTokens->catmullClark);
        }
        if (value == "loop") {
            return dst.Set(UsdGeomTokens->loop);
        }
        if (value == "bilinear") {
            return dst.Set(UsdGeomTokens->bilinear);
        }

        _PostUnsupportedValueWarning(
            object, iss, WarningSubdivisionScheme, value, "catmull-clark");
        return dst.Set(UsdGeomTokens->catmullClark);
    }
};

/// Copy an interpolate boundary from an IInt32Property.
struct _CopyInterpolateBoundary : _CopyGeneric<IInt32Property> {
    _CopyInterpolateBoundary(const AlembicProperty& object_) :
        _CopyGeneric<IInt32Property>(object_) { }

    using _CopyGeneric<IInt32Property>::operator();

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        switch (object.getValue(iss)) {
        case 1:
            return dst.Set(UsdGeomTokens->edgeAndCorner);

        case 2:
            return dst.Set(UsdGeomTokens->edgeOnly);

        default:
            _PostUnsupportedValueWarning(
                object, iss, WarningInterpolateBoundary,
                TfStringify(object.getValue(iss)).c_str(), "0");
            // Fall-through
        case 0:
            return dst.Set(UsdGeomTokens->none);
        }
    }
};

/// Copy a face varying interpolate boundary from an IInt32Property.
struct _CopyFaceVaryingInterpolateBoundary : _CopyGeneric<IInt32Property> {
    _CopyFaceVaryingInterpolateBoundary(const AlembicProperty& object_) :
        _CopyGeneric<IInt32Property>(object_) { }

    using _CopyGeneric<IInt32Property>::operator();

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        switch (object.getValue(iss)) {
        case 1:
            return dst.Set(UsdGeomTokens->cornersPlus1);

        case 2:
            return dst.Set(UsdGeomTokens->none);

        case 3:
            return dst.Set(UsdGeomTokens->boundaries);

        default:
            _PostUnsupportedValueWarning(
                object, iss, WarningFaceVaryingInterpolateBoundary,
                TfStringify(object.getValue(iss)).c_str(), "0");
            // Fall-through
        case 0:
            return dst.Set(UsdGeomTokens->all);
        }
    }
};


/// Base class to copy attributes of an alembic faceset to a USD GeomSubset
struct _CopyFaceSetBase {
    IFaceSet object;

    _CopyFaceSetBase(const IFaceSet& object_) : object(object_) { }

    bool operator()(_IsValidTag) const
    {
        return object.valid();
    }

    const MetaData& operator()(_MetaDataTag) const
    {
        return object.getMetaData();
    }

    _AlembicTimeSamples operator()(_SampleTimesTag) const
    {
        return _GetSampleTimes(object);
    }
};

/// Class to copy faceset isPartition into the family type
struct _CopyFaceSetFamilyType : _CopyFaceSetBase {
    using _CopyFaceSetBase::operator();

    _CopyFaceSetFamilyType(const IFaceSet& object_) 
        : _CopyFaceSetBase(object_) {};

    bool operator()(const UsdAbc_AlembicDataAny& dst,
                    const ISampleSelector& iss) const
    {
        // Because the absence of the ".facesExclusive" will trigger an
        // exception in IFaceSetSchema, we need to manually deal with the
        // default state (and discard the exception thrown erroneously by
        // getFaceExclusivity()).
        //
        // This is a bug in Alembic that has been fixed in Alembic 1.7.2, but 
        // the mininum required version for USD is 1.5.2. This workaround must 
        // remain until the required Alembic version is changed for USD.
        //
        // The Alembic issue can be tracked here:
        // https://github.com/alembic/alembic/issues/129
        bool isPartition = false;
        try {
            isPartition = object.getSchema().getFaceExclusivity() == kFaceSetExclusive;
        }
        catch(const Alembic::Util::Exception&) {}

        if (isPartition)
        {
            return dst.Set(UsdGeomTokens->nonOverlapping);
        }

        return dst.Set(UsdGeomTokens->unrestricted);
    }
};

static
TfToken
_ConvertCurveBasis(BasisType value)
{
    switch (value) {
    default:
    case kNoBasis:
    case kBezierBasis:
        return UsdGeomTokens->bezier;
    case kBsplineBasis:
        return UsdGeomTokens->bspline;
    case kCatmullromBasis:
        return UsdGeomTokens->catmullRom;
    case kHermiteBasis:
        TF_WARN(
            "'hermite' basis is no longer an allowed token for "
            "UsdGeomBasisCurves.");
        return UsdGeomTokens->hermite;
    case kPowerBasis:
        TF_WARN(
            "'power' basis is no longer an allowed token for "
            "UsdGeomBasisCurves.");
        return UsdGeomTokens->power;
    }
}

static
TfToken
_ConvertCurveType(CurveType value)
{
    switch (value) {
    default:
    case kCubic:
        return UsdGeomTokens->cubic;
    case kLinear:
        return UsdGeomTokens->linear;
    }
}

static
TfToken
_ConvertCurveWrap(CurvePeriodicity value)
{
    switch (value) {
    default:
    case kNonPeriodic:
        return UsdGeomTokens->nonperiodic;
    case kPeriodic:
        return UsdGeomTokens->periodic;
    }
}


//
// Object property readers
//

static
void
_ReadGprim(_PrimReaderContext* context)
{
    // Add properties.
    context->AddProperty(
        UsdGeomTokens->extent,
        SdfValueTypeNames->Float3Array,
        _CopyBoundingBox(context->ExtractSchema(".selfBnds")));

    // Consume properties implicitly handled above.
    context->Extract(GeomBaseSchemaInfo::defaultName());
}

static
void
_ReadArbGeomParams(_PrimReaderContext* context)
{
    // Add primvars.
    context->AddOutOfSchemaProperty(
        UsdAbcPropertyNames->primvars, context->ExtractSchema(".arbGeomParams"));
}

static
void
_ReadUserProperties(_PrimReaderContext* context)
{
    // Add userProperties.
    context->AddOutOfSchemaProperty(
        UsdAbcPropertyNames->userProperties,
        context->ExtractSchema(".userProperties"));
}

static
void
_ReadImageable(_PrimReaderContext* context)
{
    // Add properties.
    context->AddProperty(
        UsdGeomTokens->visibility,
        SdfValueTypeNames->Token,
        _CopyVisibility(context->Extract(kVisibilityPropertyName)));
}

static
void
_ReadMayaColor(_PrimReaderContext* context)
{
    static const TfToken displayColor("primvars:displayColor");

    // Add properties.
    context->AddProperty(
        displayColor,
        SdfValueTypeNames->Color3fArray,
        _CopyAdskColor(context->ExtractSchema("adskDiffuseColor")));
}

static
void
_ReadOther(_PrimReaderContext* context)
{
    // Read every unextracted property to Usd using default converters.
    // This handles any property we don't have specific rules for.
    for (const auto& name : context->GetUnextractedNames()) {
        context->AddOutOfSchemaProperty(
            context->GetUsdName(name), context->Extract(name));
    }
}

template<class T, class UsdValueType>
void
_ReadProperty(_PrimReaderContext* context, const char* name, TfToken propName, SdfValueTypeName typeName)
{
    // Read a generic Alembic property and convert it to a USD property.
    // If the Alembic property is indexed, this will add both the values
    // property and the indices property, in order to preserve topology.
    auto prop = context->ExtractSchema(name);
    if (prop.Cast<T>().isIndexed()) {
        context->AddProperty(
            propName,
            typeName,
            _CopyGeneric<T, UsdValueType, false>(prop));
        context->AddProperty(
            TfToken(SdfPath::JoinIdentifier(propName, UsdGeomTokens->indices)),
            SdfValueTypeNames->IntArray,
            _CopyIndices<T>(prop));
    } else {
        context->AddProperty(
            propName,
            typeName,
            _CopyGeneric<T, UsdValueType>(prop));
    }
}

static
void
_ReadOrientation(_PrimReaderContext* context)
{
    AlembicProperty orientation =
        context->Extract(_AmdName(UsdGeomTokens->orientation));
    if (orientation.Cast<IStringProperty>().valid()) {
        context->AddProperty(
            UsdGeomTokens->orientation,
            SdfValueTypeNames->Token,
            _CopyOrientation(orientation));
    } else {
        // Alembic is effectively hardcoded to a left-handed orientation.
        // UsdGeomGprim's fallback is right-handed, so we need to provide
        // a value if none is authored.
        context->AddUniformProperty(
            UsdGeomTokens->orientation,
            SdfValueTypeNames->Token,
            _CopySynthetic(UsdGeomTokens->leftHanded));
    }
}

//
// Object readers -- these set the prim type.
//

static
void
_ReadUnknown(_PrimReaderContext* context)
{
    // Set prim type.
    _PrimReaderContext::Prim& prim = context->GetPrim();
    prim.typeName =
        TfToken(context->GetObject().getMetaData().
                    get(_AmdName(SdfFieldKeys->TypeName)));
    if (prim.typeName.IsEmpty()) {
        // No type specified.  If we're a def then use Scope for lack of
        // anything better.
        if (prim.specifier == SdfSpecifierDef) {
            prim.typeName = UsdAbcPrimTypeNames->Scope;
        }
    }
}

static
void
_ReadGeomBase(_PrimReaderContext* context)
{
    _ReadUnknown(context);

    // Add child properties under schema.
    context->SetSchema(GeomBaseSchemaInfo::defaultName());
}

static
void
_ReadXform(_PrimReaderContext* context)
{
    typedef IXform Type;

    // Wrap the object.
    if (!Type::matches(context->GetObject().getHeader())) {
        // Not of type Type.
        return;
    }
    Type object(context->GetObject(), kWrapExisting);
    Type::schema_type& schema = object.getSchema();

    // Add child properties under schema.
    context->SetSchema(Type::schema_type::info_type::defaultName());

    const index_t nSamples = _GetNumSamples(schema);

    // Error checking.
    for (index_t i = 0; i < nSamples; ++i) {
        if (!schema.getInheritsXforms(ISampleSelector(i))) {
            TF_WARN("Ignoring transform that doesn't inherit at "
                    "samples at time %f at <%s>",
                    schema.getTimeSampling()->getSampleTime(i),
                    context->GetPath().GetText());
            return;
        }
    }

    // Set prim type.
    context->GetPrim().typeName = UsdAbcPrimTypeNames->Xform;

    // Add properties.
    if (nSamples > 0) {
        // We could author individual component transforms here, just 
        // as the transform is represented in alembic, but round-tripping 
        // will be an issue because of the way the alembicWriter reads
        // transforms out of USD. 
        // 
        // For now, we're exporting the composed transform value, until 
        // we figure out a solution to the round-tripping problem.
        // 
        context->AddProperty(
            _tokens->xformOpTransform,
            SdfValueTypeNames->Matrix4d,
            _CopyXform(object));

        VtTokenArray opOrderVec(1);
        opOrderVec[0] = _tokens->xformOpTransform;
        context->AddUniformProperty(
            UsdGeomTokens->xformOpOrder,
            SdfValueTypeNames->TokenArray,
            _CopySynthetic(opOrderVec));
    }

    // Consume properties implicitly handled above.
    context->Extract(Type::schema_type::info_type::defaultName());
}

static
void
_ReadPolyMesh(_PrimReaderContext* context)
{
    typedef IPolyMesh Type;

    // Wrap the object.
    if (!Type::matches(context->GetObject().getHeader())) {
        // Not of type Type.
        return;
    }

    // Set prim type.
    context->GetPrim().typeName = UsdAbcPrimTypeNames->Mesh;

    // Add child properties under schema.
    context->SetSchema(Type::schema_type::info_type::defaultName());

    // Add properties.
    context->AddProperty(
        UsdGeomTokens->points,
        SdfValueTypeNames->Point3fArray,
        _CopyGeneric<IP3fArrayProperty, GfVec3f>(
            context->ExtractSchema("P"), kNoMatching));
    context->AddProperty(
        UsdGeomTokens->velocities,
        SdfValueTypeNames->Vector3fArray,
        _CopyGeneric<IV3fArrayProperty, GfVec3f>(
            context->ExtractSchema(".velocities")));
    context->AddProperty(
        UsdGeomTokens->normals,
        SdfValueTypeNames->Normal3fArray,
        _CopyGeneric<IN3fGeomParam, GfVec3f>(
            context->ExtractSchema("N")));
    context->AddProperty(
        UsdGeomTokens->faceVertexIndices,
        SdfValueTypeNames->IntArray,
        _CopyGeneric<IInt32ArrayProperty, int>(
            context->ExtractSchema(".faceIndices")));
    context->AddProperty(
        UsdGeomTokens->faceVertexCounts,
        SdfValueTypeNames->IntArray,
        _CopyGeneric<IInt32ArrayProperty, int>(
            context->ExtractSchema(".faceCounts")));

    // Read texture coordinates
    _ReadProperty<IV2fGeomParam, GfVec2f>(context, "uv", _GetUVPropertyName(), _GetUVTypeName());

    // Custom subdivisionScheme property.  Alembic doesn't have this since
    // the Alembic schema is PolyMesh.  Usd needs "none" as the scheme.
    context->AddUniformProperty(
        UsdGeomTokens->subdivisionScheme,
        SdfValueTypeNames->Token,
        _CopySynthetic(UsdGeomTokens->none));
}

static
void
_ReadSubD(_PrimReaderContext* context)
{
    typedef ISubD Type;

    // Wrap the object.
    if (!Type::matches(context->GetObject().getHeader())) {
        // Not of type Type.
        return;
    }

    // Set prim type.
    context->GetPrim().typeName = UsdAbcPrimTypeNames->Mesh;

    // Add child properties under schema.
    context->SetSchema(Type::schema_type::info_type::defaultName());

    // Add properties.
    context->AddProperty(
        UsdGeomTokens->points,
        SdfValueTypeNames->Point3fArray,
        _CopyGeneric<IP3fArrayProperty, GfVec3f>(
            context->ExtractSchema("P"), kNoMatching));
    context->AddProperty(
        UsdGeomTokens->velocities,
        SdfValueTypeNames->Vector3fArray,
        _CopyGeneric<IV3fArrayProperty, GfVec3f>(
            context->ExtractSchema(".velocities")));
    context->AddProperty(
        UsdGeomTokens->faceVertexIndices,
        SdfValueTypeNames->IntArray,
        _CopyGeneric<IInt32ArrayProperty, int>(
            context->ExtractSchema(".faceIndices")));
    context->AddProperty(
        UsdGeomTokens->faceVertexCounts,
        SdfValueTypeNames->IntArray,
        _CopyGeneric<IInt32ArrayProperty, int>(
            context->ExtractSchema(".faceCounts")));
    context->AddUniformProperty(
        UsdGeomTokens->subdivisionScheme,
        SdfValueTypeNames->Token,
        _CopySubdivisionScheme(context->ExtractSchema(".scheme")));
    context->AddProperty(
        UsdGeomTokens->interpolateBoundary,
        SdfValueTypeNames->Token,
        _CopyInterpolateBoundary(
            context->ExtractSchema(".interpolateBoundary")));
    context->AddProperty(
        UsdGeomTokens->faceVaryingLinearInterpolation,
        SdfValueTypeNames->Token,
        _CopyFaceVaryingInterpolateBoundary(
            context->ExtractSchema(".faceVaryingInterpolateBoundary")));
    context->AddProperty(
        UsdGeomTokens->holeIndices,
        SdfValueTypeNames->IntArray,
        _CopyGeneric<IInt32ArrayProperty, int>(
            context->ExtractSchema(".holes")));
    context->AddProperty(
        UsdGeomTokens->cornerIndices,
        SdfValueTypeNames->IntArray,
        _CopyGeneric<IInt32ArrayProperty, int>(
            context->ExtractSchema(".cornerIndices")));
    context->AddProperty(
        UsdGeomTokens->cornerSharpnesses,
        SdfValueTypeNames->FloatArray,
        _CopyGeneric<IFloatArrayProperty, float>(
            context->ExtractSchema(".cornerSharpnesses")));
    context->AddProperty(
        UsdGeomTokens->creaseIndices,
        SdfValueTypeNames->IntArray,
        _CopyGeneric<IInt32ArrayProperty, int>(
            context->ExtractSchema(".creaseIndices")));
    context->AddProperty(
        UsdGeomTokens->creaseLengths,
        SdfValueTypeNames->IntArray,
        _CopyGeneric<IInt32ArrayProperty, int>(
            context->ExtractSchema(".creaseLengths")));
    context->AddProperty(
        UsdGeomTokens->creaseSharpnesses,
        SdfValueTypeNames->FloatArray,
        _CopyGeneric<IFloatArrayProperty, float>(
            context->ExtractSchema(".creaseSharpnesses")));

    // Read texture coordinates
    _ReadProperty<IV2fGeomParam, GfVec2f>(context, "uv", _GetUVPropertyName(), _GetUVTypeName());
}

static
void
_ReadFaceSet(_PrimReaderContext* context)
{
    typedef IFaceSet Type;

    // Wrap the object.
    if (!Type::matches(context->GetObject().getHeader())) {
        // Not of type Type.
        return;
    }

    // A FaceSet must be a child of another Alembic object.
    if (!context->GetObject().getParent()) {
        // No parent exists.
        return;
    }

    Type object(context->GetObject(), kWrapExisting);

    // Add child properties under schema.
    context->SetSchema(Type::schema_type::info_type::defaultName());

    // Set prim type.
    context->GetPrim().typeName = UsdAbcPrimTypeNames->GeomSubset;

    context->AddProperty(
        UsdGeomTokens->indices,
        SdfValueTypeNames->IntArray,
        _CopyGeneric<IInt32ArrayProperty, int>(
            context->ExtractSchema(".faces")));
    context->AddUniformProperty(
        UsdGeomTokens->elementType,
        SdfValueTypeNames->Token,
        _CopySynthetic(UsdGeomTokens->face));

    context->AddUniformProperty(
        UsdGeomTokens->familyName,
        SdfValueTypeNames->Token,
        _CopySynthetic(UsdAbcPropertyNames->defaultFamilyName));

    _PrimReaderContext parentPrimContext = context->GetParentContext();

    parentPrimContext.AddUniformProperty(
        UsdAbcPropertyNames->defaultFamilyTypeAttributeName,
        SdfValueTypeNames->Token,
        _CopyFaceSetFamilyType(object));

    // Consume properties implicitly handled above.
    context->Extract(Type::schema_type::info_type::defaultName());
}

static
void
_ReadCurves(_PrimReaderContext* context)
{
    typedef ICurves Type;

    // Wrap the object.
    if (!Type::matches(context->GetObject().getHeader())) {
        // Not of type Type.
        return;
    }

    // Add child properties under schema.
    context->SetSchema(Type::schema_type::info_type::defaultName());

    // An Alembic can animate the curve type, basis and periodicity but
    // Usd cannot.  We'll simply take the first sample's values and use
    // them for all samples.  We also extract and ignore the basis, type
    // and periodicity property (they're all packed into one property).
    Type::schema_type::Sample sample;
    if (!context->GetSample<Type>(sample, ISampleSelector())) {
        // Doesn't appear to have the right type.
        return;
    }
    (void)context->ExtractSchema("curveBasisAndType");

    // Set prim type.  This depends on the CurveType of the curve.
    if (sample.getType() == kVariableOrder){
        context->GetPrim().typeName = UsdAbcPrimTypeNames->NurbsCurves;
        context->AddProperty(
            UsdGeomTokens->order,
            SdfValueTypeNames->IntArray,
            _CopyGeneric<IInt32ArrayProperty, int>(
                context->ExtractSchema(".orders")));
        context->AddProperty(
            UsdGeomTokens->knots,
            SdfValueTypeNames->DoubleArray,
            _CopyGeneric<IFloatArrayProperty, double>(
                context->ExtractSchema(".knots")));
    } else if ((sample.getType() == kCubic) &&
               (sample.getBasis() == kHermiteBasis)) {
        context->GetPrim().typeName = UsdAbcPrimTypeNames->HermiteCurves;
        if (_ConvertCurveWrap(sample.getWrap()) != UsdGeomTokens->nonperiodic) {
            TF_WARN("Wrap mode is not supported for cubic hermite curves: '%s'",
                    context->GetPath().GetText());
        }
        if (_CopyGeneric<IV3fArrayProperty, GfVec3f>(
                context->ExtractSchema(".velocities"))(_IsValidTag())) {
            TF_WARN(
                "Velocities are not supported for cubic hermite curves: '%s'",
                context->GetPath().GetText());
        }
        // The Usd representation separates points and tangents
        // The Alembic representation interleaves them as (P0, T0, ... Pn, Tn)
        context->AddProperty(
            UsdGeomTokens->points,
            SdfValueTypeNames->Point3fArray,
            _CopyHermitePoints(context->ExtractSchema("P")));
        context->AddProperty(
            UsdGeomTokens->tangents,
            SdfValueTypeNames->Vector3fArray,
            _CopyHermiteTangents(context->ExtractSchema("P")));
    } else {
        context->GetPrim().typeName = UsdAbcPrimTypeNames->BasisCurves;
        context->AddUniformProperty(
            UsdGeomTokens->type,
            SdfValueTypeNames->Token,
            _CopySynthetic(_ConvertCurveType(sample.getType())));
        context->AddUniformProperty(
            UsdGeomTokens->basis,
            SdfValueTypeNames->Token,
            _CopySynthetic(_ConvertCurveBasis(sample.getBasis())));
        context->AddUniformProperty(
            UsdGeomTokens->wrap,
            SdfValueTypeNames->Token,
            _CopySynthetic(_ConvertCurveWrap(sample.getWrap())));
    }

    if ((context->GetPrim().typeName == UsdAbcPrimTypeNames->BasisCurves) ||
        (context->GetPrim().typeName == UsdAbcPrimTypeNames->NurbsCurves)){
        context->AddProperty(
            UsdGeomTokens->points,
            SdfValueTypeNames->Point3fArray,
            _CopyGeneric<IP3fArrayProperty, GfVec3f>(
                context->ExtractSchema("P"), kNoMatching));
        context->AddProperty(
            UsdGeomTokens->velocities,
            SdfValueTypeNames->Vector3fArray,
            _CopyGeneric<IV3fArrayProperty, GfVec3f>(
                context->ExtractSchema(".velocities")));
    }

    context->AddProperty(
        UsdGeomTokens->normals,
        SdfValueTypeNames->Normal3fArray,
        _CopyGeneric<IN3fGeomParam, GfVec3f>(
            context->ExtractSchema("N")));
    context->AddProperty(
        UsdGeomTokens->curveVertexCounts,
        SdfValueTypeNames->IntArray,
        _CopyGeneric<IInt32ArrayProperty, int>(
            context->ExtractSchema("nVertices")));
    context->AddProperty(
        UsdGeomTokens->widths,
        SdfValueTypeNames->FloatArray,
        _CopyGeneric<IFloatGeomParam, float>(
            context->ExtractSchema("width")));
}

static
void
_ReadPoints(_PrimReaderContext* context)
{
    typedef IPoints Type;

    // Wrap the object.
    if (!Type::matches(context->GetObject().getHeader())) {
        // Not of type Type.
        return;
    }

    // Set prim type.
    context->GetPrim().typeName = UsdAbcPrimTypeNames->Points;

    // Add child properties under schema.
    context->SetSchema(Type::schema_type::info_type::defaultName());

    // Add properties.
    context->AddProperty(
        UsdGeomTokens->points,
        SdfValueTypeNames->Point3fArray,
        _CopyGeneric<IP3fArrayProperty, GfVec3f>(
            context->ExtractSchema("P"), kNoMatching));
    context->AddProperty(
        UsdGeomTokens->velocities,
        SdfValueTypeNames->Vector3fArray,
        _CopyGeneric<IV3fArrayProperty, GfVec3f>(
            context->ExtractSchema(".velocities")));
    context->AddProperty(
        UsdGeomTokens->widths,
        SdfValueTypeNames->FloatArray,
        _CopyGeneric<IFloatGeomParam, float>(
            context->ExtractSchema(".widths")));
    context->AddProperty(
        UsdGeomTokens->ids,
        SdfValueTypeNames->Int64Array,
        _CopyGeneric<IUInt64ArrayProperty, int64_t>(
            context->ExtractSchema(".pointIds")));
}

static
void
_ReadCameraParameters(_PrimReaderContext* context)
{
    typedef ICamera Type;

    // Wrap the object.
    if (!Type::matches(context->GetObject().getHeader())) {
        // Not of type Type.
        return;
    }
    Type object(context->GetObject(), kWrapExisting);

    // Set prim type.
    context->GetPrim().typeName = UsdAbcPrimTypeNames->Camera;
    
    // Add child properties under schema.
    context->SetSchema(
        Type::schema_type::info_type::defaultName());

    // Add the minimal set of properties to set up the
    // camera frustum.
    context->AddProperty(
        UsdGeomTokens->focalLength,
        SdfValueTypeNames->Float,
        _CopyCameraFocalLength(object));
    context->AddProperty(
        UsdGeomTokens->horizontalAperture,
        SdfValueTypeNames->Float,
        _CopyCameraHorizontalAperture(object));
    context->AddProperty(
        UsdGeomTokens->verticalAperture,
        SdfValueTypeNames->Float,
        _CopyCameraVerticalAperture(object));
    context->AddProperty(
        UsdGeomTokens->horizontalApertureOffset,
        SdfValueTypeNames->Float,
        _CopyCameraHorizontalApertureOffset(object));
    context->AddProperty(
        UsdGeomTokens->verticalApertureOffset,
        SdfValueTypeNames->Float,
        _CopyCameraVerticalApertureOffset(object));
    context->AddProperty(
        UsdGeomTokens->clippingRange,
        SdfValueTypeNames->Float2,
        _CopyCameraClippingRange(object));

    // Extract all other alembic camera properties so that they don't show up
    // in USD.
    // In particular, alembic camera back xforms are backed out and should
    // not show up in USD.
    context->Extract(Type::schema_type::info_type::defaultName());
}

static
_ReaderContext::Ordering
_GetOrderingMetadata(
    const MetaData& alembicMetadata,
    const TfToken& field)
{
    const std::string& value = alembicMetadata.get(_AmdName(field));
    if (!value.empty()) {
        const std::vector<std::string> names = TfStringTokenize(value, " []");
        if (!names.empty()) {
            return _ReaderContext::Ordering(
                        TfTokenVector(names.begin(), names.end()));
        }
    }
    return _ReaderContext::Ordering();
}

static
void
_GetPrimMetadata(const MetaData& metadata, _ReaderContext::Prim& prim)
{
    // def/over.
    if (metadata.get(_AmdName(SdfFieldKeys->Specifier)) == "over") {
        prim.specifier = SdfSpecifierOver;
    }
    else {
        prim.specifier = SdfSpecifierDef;
    }

    // Other metadata.
    _GetBoolMetadata(metadata, prim.metadata, SdfFieldKeys->Active);
    _GetBoolMetadata(metadata, prim.metadata, SdfFieldKeys->Hidden);
    _GetStringMetadata(metadata, prim.metadata, SdfFieldKeys->DisplayGroup);
    _GetStringMetadata(metadata, prim.metadata, SdfFieldKeys->Documentation);
    _GetTokenMetadata(metadata, prim.metadata, SdfFieldKeys->Kind);

    // Add name children ordering.
    prim.primOrdering =
        _GetOrderingMetadata(metadata, SdfFieldKeys->PrimOrder);

    // Add property ordering.
    prim.propertyOrdering =
        _GetOrderingMetadata(metadata, SdfFieldKeys->PropertyOrder);
}

static
std::string
_ComputeSchemaName(
    const _ReaderContext& context,
    const IObject& object)
{
    // Special case where we stored the type.  Note that we can't assume
    // that this is accurate.  For example, this might say its an Xform
    // but we must be prepared for it not to actually be an Alembic Xform.
    std::string value =
        object.getMetaData().get(_AmdName(SdfFieldKeys->TypeName));
    if (!value.empty()) {
        return value;
    }

    // General case.
    const std::string schema = object.getMetaData().get("schema");

    // Special cases.  If there's no schema try the base type.
    if (schema.empty()) {
        return object.getMetaData().get("schemaBaseType");
    }

    return schema;
}

template <typename TYPE>
static
ICompoundProperty
_GetSchemaProperty(const IObject& object)
{
    return ICompoundProperty(object.getProperties(),
                             TYPE::schema_type::info_type::defaultName(),
                             ErrorHandler::kQuietNoopPolicy);
}

static
std::string
_ReadPrim(
    _ReaderContext& context,
    const IObject& object,
    const SdfPath& parentPath,
    std::set<std::string>* usedSiblingNames)
{
    // Choose the name.
    std::string name = _CleanName(object.getName(), " _", *usedSiblingNames,
                                  _AlembicFixName(),
                                  &SdfPath::IsValidIdentifier);
    usedSiblingNames->insert(name);
    SdfPath path = parentPath.AppendChild(TfToken(name));

    // Compute the schema name.
    const std::string schemaName = _ComputeSchemaName(context, object);

    // Handle non-instances.
    _ReaderContext::Prim* instance = nullptr;
    if (!context.IsInstance(object)) {
        // Combine geom with parent if parent is a transform.  There are
        // several cases where we want to bail out and, rather than use a
        // huge if statement or deep if nesting, we'll use do/while and
        // break to do it.
        do {
            if (!TfGetEnvSetting(USD_ABC_XFORM_PRIM_COLLAPSE)) {
                // Transform collapse is specified as unwanted behavior
                break;
            }
            // Parent has to be a transform.
            IObject parent = object.getParent();
            if (!IXform::matches(parent.getHeader())) {
                break;
            }
            ICompoundProperty parentProperties =
                _GetSchemaProperty<IXform>(parent);
            if (!parentProperties.valid()) {
                break;
            }

            // The parent must not be the root of an instance.
            if (context.IsInstance(parent)) {
                break;
            }

            // This object must be an IGeomBase or ICamera.
            // XXX: May want to be more selective here.
            ICompoundProperty objectProperties;
            if (IGeomBase::matches(object.getMetaData())) {
                objectProperties = _GetSchemaProperty<IGeomBaseObject>(object);
            }
            else if (ICamera::matches(object.getMetaData(),
                                     kSchemaTitleMatching)) {
                objectProperties = _GetSchemaProperty<ICamera>(object);
            }
            if (!objectProperties.valid()) {
                break;
            }

            // We can't merge .arbGeomParams and .userProperties so
            // bail if either are in both this object and the parent.
            if (objectProperties.getPropertyHeader(".arbGeomParams") && 
                parentProperties.getPropertyHeader(".arbGeomParams")) {
                break;
            }
            if (objectProperties.getPropertyHeader(".userProperties") && 
                parentProperties.getPropertyHeader(".userProperties")) {
                break;
            }

            // We can combine!  Cache into our parent's entry.
            path = parentPath;

            // Don't add this object to the parent's children.
            name.clear();
        } while (false);
    }

    // If this is an instance then we create a prim at the path and
    // give it a reference to the prototype.  Then we change the path to
    // be that of the prototype and continue traversal.  This puts the
    // entire prototype hierarchy under the new path and puts a simple
    // prim with nothing but a reference at the old path.
    else {
        instance = &context.AddInstance(path, object);
        if (!instance->prototype.IsEmpty()) {
            path = instance->prototype;
        }
        else {
            instance = nullptr;
            const std::string prototypePath =
                object.isInstanceRoot()
                    ? IObject(object).instanceSourcePath()
                    : object.getFullName();
            TF_CODING_ERROR(
                "Instance %s has no prototype at %s.",
                object.getFullName().c_str(),
                prototypePath.c_str());
            // Continue and we'll simply expand the instance.
        }
    }

    // At this point if instance != nullptr then we're instancing,
    // path points to the prototype, and instance points to the instance
    // prim's cache.

    // If the instance source was promoted then we need to copy the
    // prim's metadata and properties to the instance.  But we don't
    // need quite everything because the prototype will supply some of it.
    // For simplicity, we copy all data as usual then discard what we
    // don't want.
    if (instance && instance->promoted) {
        // Read the metadata.
        _GetPrimMetadata(object.getMetaData(), *instance);

        // Read the properties.  Reconstruct the instance path since we've
        // changed path to point at the prototype.
        const SdfPath instancePath = parentPath.AppendChild(TfToken(name));
        _PrimReaderContext primContext(context, object, instancePath);
        for (const auto& reader : context.GetSchema().GetPrimReaders(schemaName)) {
            TRACE_SCOPE("UsdAbc_AlembicDataReader:_ReadPrim");
            reader(&primContext);
        }

        // Discard name children ordering since we don't have any name
        // children (except via the prototype reference).
        instance->primOrdering = boost::none;
    }

    // Get the prim cache.  If instance is true then prim is the prototype,
    // otherwise it's a non-instanced prim or a descendant of a prototype.
    _ReaderContext::Prim& prim = context.AddPrim(path);

    // If we're instancing but the prototype prim cache already has a type
    // name then we've already found a previous instance of this prototype
    // and we've already traversed the prototype once.  Don't traverse a
    // prototype again.
    if (!instance || prim.typeName.IsEmpty()) {
        // Add prim metadata.
        _GetPrimMetadata(object.getMetaData(), prim);

#ifdef USDABC_ALEMBIC_DEBUG
        fprintf(stdout, "%*s%s%s%s \"%s\" { # %s, %s\n",
                2 * (int)(path.GetPathElementCount() - 1), "",
                prim.specifier == SdfSpecifierOver ? "over" : "def",
                prim.typeName.IsEmpty() ? "" : " ",
                prim.typeName.GetText(),
                name.empty() ? "<merge-with-parent>" : name.c_str(),
                schemaName.c_str(), object.getName().c_str());
#endif

        if (path != SdfPath::AbsoluteRootPath()) {
            // Read the properties.
            _PrimReaderContext primContext(context, object, path);
            for (const auto& reader : context.GetSchema().GetPrimReaders(schemaName)) {
                TRACE_SCOPE("UsdAbc_AlembicDataReader:_ReadPrim");
                reader(&primContext);
            }
        }

        // Recurse.
        _ReadPrimChildren(context, object, path, prim);

#ifdef USDABC_ALEMBIC_DEBUG
        fprintf(stdout, "%*s}\n",
                2 * (int)(path.GetPathElementCount() - 1), "");
#endif

        // If the instance source was promoted then we don't need or want
        // any of the instance source's properties on the prototype since each
        // Usd instance will have its own.  We also don't want the prototype
        // to have most metadata for the same reason.  For the sake of
        // simplicity of the code, we already copied all of that info so
        // we'll discard it now.
        if (instance && instance->promoted) {
            // prim is the prototype.
            prim.properties.clear();
            prim.propertyOrdering = boost::none;
            prim.metadata.clear();
            prim.propertiesCache.clear();
        }

        // If this is a prototype then make it an over.
        if (instance) {
            prim.specifier = SdfSpecifierOver;
        }
    }

    // Modify the metadata for an instance.  We wait until now because we
    // want to get the prototype's type name.
    if (instance) {
        instance->typeName  = prim.typeName;
        instance->specifier = SdfSpecifierDef;
    }

    return name;
}

static
void
_ReadPrimChildren(
    _ReaderContext& context,
    const IObject& object,
    const SdfPath& path,
    _ReaderContext::Prim& prim)
{
    // Collect children names.  By prepopulating usedNames we ensure that
    // the child with the valid name gets its name even if a child with a
    // lower index has a name that mangles to the valid name.
    std::set<std::string> usedNames;
    for (size_t i = 0, n = object.getNumChildren(); i != n; ++i) {
        usedNames.insert(object.getChildHeader(i).getName());
    }

    // Read the children.
    for (size_t i = 0, n = object.getNumChildren(); i != n; ++i) {
        IObject child(object, object.getChildHeader(i).getName());
        const std::string childName =
            _ReadPrim(context, child, path, &usedNames);
        if (!childName.empty()) {
            prim.children.push_back(TfToken(childName));
        }
    }
}

// ----------------------------------------------------------------------------

//
// Schema builder
//

struct _ReaderSchemaBuilder {
    _ReaderSchema schema;

    _ReaderSchemaBuilder();
};

_ReaderSchemaBuilder::_ReaderSchemaBuilder()
{
    schema.AddType(GeomBaseSchemaInfo::title())
        .AppendReader(_ReadGeomBase)
        .AppendReader(_ReadMayaColor)
        .AppendReader(_ReadGprim)
        .AppendReader(_ReadImageable)
        .AppendReader(_ReadArbGeomParams)
        .AppendReader(_ReadUserProperties)
        .AppendReader(_ReadOther)
        ;
    schema.AddType(XformSchemaInfo::title())
        .AppendReader(_ReadXform)
        .AppendReader(_ReadImageable)
        .AppendReader(_ReadArbGeomParams)
        .AppendReader(_ReadUserProperties)
        .AppendReader(_ReadOther)
        ;
    schema.AddType(SubDSchemaInfo::title())
        .AppendReader(_ReadOrientation)
        .AppendReader(_ReadSubD)
        .AppendReader(_ReadMayaColor)
        .AppendReader(_ReadGprim)
        .AppendReader(_ReadImageable)
        .AppendReader(_ReadArbGeomParams)
        .AppendReader(_ReadUserProperties)
        .AppendReader(_ReadOther)
        ;
    schema.AddType(PolyMeshSchemaInfo::title())
        .AppendReader(_ReadOrientation)
        .AppendReader(_ReadPolyMesh)
        .AppendReader(_ReadMayaColor)
        .AppendReader(_ReadGprim)
        .AppendReader(_ReadImageable)
        .AppendReader(_ReadArbGeomParams)
        .AppendReader(_ReadUserProperties)
        .AppendReader(_ReadOther)
        ;
    schema.AddType(FaceSetSchemaInfo::title())
        .AppendReader(_ReadFaceSet)
        ;
    schema.AddType(CurvesSchemaInfo::title())
        .AppendReader(_ReadOrientation)
        .AppendReader(_ReadCurves)
        .AppendReader(_ReadMayaColor)
        .AppendReader(_ReadGprim)
        .AppendReader(_ReadImageable)
        .AppendReader(_ReadArbGeomParams)
        .AppendReader(_ReadUserProperties)
        .AppendReader(_ReadOther)
        ;
    schema.AddType(PointsSchemaInfo::title())
        .AppendReader(_ReadOrientation)
        .AppendReader(_ReadPoints)
        .AppendReader(_ReadMayaColor)
        .AppendReader(_ReadGprim)
        .AppendReader(_ReadImageable)
        .AppendReader(_ReadArbGeomParams)
        .AppendReader(_ReadUserProperties)
        .AppendReader(_ReadOther)
        ;
    schema.AddType(CameraSchemaInfo::title())
        .AppendReader(_ReadCameraParameters)
        .AppendReader(_ReadArbGeomParams)
        .AppendReader(_ReadUserProperties)
        .AppendReader(_ReadOther)
        ;

    // This handles overs with no type and any unknown prim type.
    schema.AddFallbackType()
        .AppendReader(_ReadGeomBase)        // Assume GeomBase.
        .AppendReader(_ReadMayaColor)
        .AppendReader(_ReadGprim)
        .AppendReader(_ReadImageable)
        .AppendReader(_ReadArbGeomParams)
        .AppendReader(_ReadUserProperties)
        .AppendReader(_ReadOther)
        ;
}

} // anonymous namespace

static
const _ReaderSchema&
_GetSchema()
{
    static _ReaderSchemaBuilder builder;
    return builder.schema;
}

//
// UsdAbc_AlembicDataReader::TimeSamples
//


UsdAbc_AlembicDataReader::TimeSamples::TimeSamples()
{
    // Do nothing
}

UsdAbc_AlembicDataReader::TimeSamples::TimeSamples(
    const std::vector<double>& times) :
    _times(times)
{
    // Do nothing
}

void
UsdAbc_AlembicDataReader::TimeSamples::Swap(TimeSamples& other)
{
    _times.swap(other._times);
}

bool
UsdAbc_AlembicDataReader::TimeSamples::IsEmpty() const
{
    return _times.empty();
}

size_t
UsdAbc_AlembicDataReader::TimeSamples::GetSize() const
{
    return _times.size();
}

UsdAbc_TimeSamples
UsdAbc_AlembicDataReader::TimeSamples::GetTimes() const
{
    return UsdAbc_TimeSamples(_times.begin(), _times.end());
}

double
UsdAbc_AlembicDataReader::TimeSamples::operator[](size_t index) const
{
    return _times[index];
}

void
UsdAbc_AlembicDataReader::TimeSamples::AddTo(UsdAbc_TimeSamples* samples) const
{
    samples->insert(_times.begin(), _times.end());
}

bool
UsdAbc_AlembicDataReader::TimeSamples::FindIndex(double usdTime,Index* index) const
{
    _UsdTimeCodes::const_iterator i =
        std::lower_bound(_times.begin(), _times.end(), usdTime);
    if (i == _times.end() || *i != usdTime) {
        return false;
    }
    else {
        *index = std::distance(_times.begin(), i);
        return true;
    }
}

template <class T>
static
typename std::vector<T>::const_iterator
UsdAbc_lower_bound(const std::vector<T>& c, T x)
{
    return std::lower_bound(c.begin(), c.end(), x);
}

template <class T>
static
typename std::set<T>::const_iterator
UsdAbc_lower_bound(const std::set<T>& c, T x)
{
    return c.lower_bound(x);
}

template <class T>
bool
UsdAbc_AlembicDataReader::TimeSamples::Bracket(
    const T& samples, double usdTime,
    double* tLower, double* tUpper)
{
    if (samples.empty()) {
        return false;
    }

    typename T::const_iterator i = UsdAbc_lower_bound(samples, usdTime);
    if (i == samples.end()) {
        // Past last sample.
        *tLower = *tUpper = *--i;
    }
    else if (i == samples.begin() || *i == usdTime) {
        // Before first sample or at a sample.
        *tLower = *tUpper = *i;
    }
    else {
        // Bracket a sample.
        *tUpper = *i;
        *tLower = *--i;
    }
    return true;
}

// Instantiate for UsdAbc_TimeSamples.
template
bool
UsdAbc_AlembicDataReader::TimeSamples::Bracket(
    const UsdAbc_TimeSamples& samples, double usdTime,
    double* tLower, double* tUpper);

bool
UsdAbc_AlembicDataReader::TimeSamples::Bracket(
    double usdTime, double* tLower, double* tUpper) const
{
    return Bracket(_times, usdTime, tLower, tUpper);
}

//
// UsdAbc_AlembicDataReader
//

class UsdAbc_AlembicDataReaderImpl : public _ReaderContext { };

UsdAbc_AlembicDataReader::UsdAbc_AlembicDataReader() :
    _impl(new UsdAbc_AlembicDataReaderImpl)
{
    _impl->SetSchema(&_GetSchema());
}

UsdAbc_AlembicDataReader::~UsdAbc_AlembicDataReader()
{
    Close();
}

bool
UsdAbc_AlembicDataReader::Open(const std::string& filePath,
                               const SdfFileFormat::FileFormatArguments& args)
{
    TRACE_FUNCTION();

    _errorLog.clear();
    try {
        if (_impl->Open(filePath, &_errorLog, args)) {
            return true;
        }
    }
    catch (std::exception& e) {
        _errorLog.append(e.what());
        _errorLog.append("\n");
    }
    return false;
}

void
UsdAbc_AlembicDataReader::Close()
{
    _impl->Close();
}

std::string
UsdAbc_AlembicDataReader::GetErrors() const
{
    return _errorLog;
}

void
UsdAbc_AlembicDataReader::SetFlag(const TfToken& flagName, bool set)
{
    _impl->SetFlag(flagName, set);
}

bool
UsdAbc_AlembicDataReader::HasSpec(const SdfPath& path) const
{
    return _impl->HasSpec(path);
}

SdfSpecType
UsdAbc_AlembicDataReader::GetSpecType(const SdfPath& path) const
{
    return _impl->GetSpecType(path);
}

bool
UsdAbc_AlembicDataReader::HasField(
    const SdfPath& path,
    const TfToken& fieldName,
    SdfAbstractDataValue* value) const
{
    return _impl->HasField(path, fieldName, UsdAbc_AlembicDataAny(value));
}

bool
UsdAbc_AlembicDataReader::HasField(
    const SdfPath& path,
    const TfToken& fieldName,
    VtValue* value) const
{
    return _impl->HasField(path, fieldName, UsdAbc_AlembicDataAny(value));
}

bool
UsdAbc_AlembicDataReader::HasValue(
    const SdfPath& path,
    Index index,
    SdfAbstractDataValue* value) const
{
    return _impl->HasValue(path, index, UsdAbc_AlembicDataAny(value));
}

bool
UsdAbc_AlembicDataReader::HasValue(
    const SdfPath& path,
    Index index,
    VtValue* value) const
{
    return _impl->HasValue(path, index, UsdAbc_AlembicDataAny(value));
}

void
UsdAbc_AlembicDataReader::VisitSpecs(
    const SdfAbstractData& owner,
    SdfAbstractDataSpecVisitor* visitor) const
{
    return _impl->VisitSpecs(owner, visitor);
}

TfTokenVector
UsdAbc_AlembicDataReader::List(const SdfPath& path) const
{
    return _impl->List(path);
}

const std::set<double>&
UsdAbc_AlembicDataReader::ListAllTimeSamples() const
{
    return _impl->ListAllTimeSamples();
}

const UsdAbc_AlembicDataReader::TimeSamples&
UsdAbc_AlembicDataReader::ListTimeSamplesForPath(
    const SdfPath& path) const
{
    return _impl->ListTimeSamplesForPath(path);
}

PXR_NAMESPACE_CLOSE_SCOPE

