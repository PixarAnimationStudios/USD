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
#ifndef USDSHADE_GENERATED_MATERIALBINDINGAPI_H
#define USDSHADE_GENERATED_MATERIALBINDINGAPI_H

/// \file usdShade/materialBindingAPI.h

#include "pxr/pxr.h"
#include "pxr/usd/usdShade/api.h"
#include "pxr/usd/usd/apiSchemaBase.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdShade/tokens.h"

#include "pxr/usd/usd/collectionAPI.h"
#include "pxr/usd/usdGeom/subset.h"
#include "pxr/usd/usdShade/material.h"
#include <tbb/concurrent_unordered_map.h>

#include "pxr/base/vt/value.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfAssetPath;

// -------------------------------------------------------------------------- //
// MATERIALBINDINGAPI                                                         //
// -------------------------------------------------------------------------- //

/// \class UsdShadeMaterialBindingAPI
///
/// UsdShadeMaterialBindingAPI is an API schema that provides an 
/// interface for binding materials to prims or collections of prims 
/// (represented by UsdCollectionAPI objects). 
/// 
/// In the USD shading model, each renderable gprim computes a single 
/// <b>resolved Material</b> that will be used to shade the gprim (exceptions, 
/// of course, for gprims that possess UsdGeomSubsets, as each subset can be 
/// shaded by a different Material).  A gprim <b>and each of its ancestor 
/// prims</b> can possess, through the MaterialBindingAPI, both a 
/// <b>direct</b> binding to a Material, and any number of 
/// <b>collection-based</b> bindings to Materials; each binding can be generic 
/// or declared for a particular <b>purpose</b>, and given a specific <b>binding 
/// strength</b>. It is the process of "material resolution" (see 
/// \ref UsdShadeMaterialBindingAPI_MaterialResolution) that examines all of 
/// these bindings, and selects the one Material that best matches the 
/// client's needs.
/// 
/// The intent of <b>purpose</b> is that each gprim should be able to resolve a 
/// Material for any given purpose, which implies it can have differently bound 
/// materials for different purposes. There are two <i>special</i> values of 
/// <b>purpose</b> defined in UsdShade, although the API fully supports 
/// specifying arbitrary values for it, for the sake of extensibility:
/// <ul><li><b>UsdShadeTokens->full</b>: to be used when the purpose of the 
/// render is entirely to visualize the truest representation of a scene, 
/// considering all lighting and material information, at highest fidelity.</li>  
/// <li><b>UsdShadeTokens->preview</b>: to be used when the render is in 
/// service of a goal other than a high fidelity "full" render (such as scene
/// manipulation, modeling, or realtime playback). Latency and speed are 
/// generally of greater concern for preview renders, therefore preview 
/// materials are generally designed to be "lighterweight" compared to full
/// materials.</li></ul>
/// A binding can also have no specific purpose at all, in which 
/// case, it is considered to be the fallback or all-purpose binding (denoted 
/// by the empty-valued token <b>UsdShadeTokens->allPurpose</b>). 
/// 
/// The <b>purpose</b> of a material binding is encoded in the name of the 
/// binding relationship. 
/// <ul><li>
/// In the case of a direct binding, the <i>allPurpose</i> binding is 
/// represented by the relationship named <b>"material:binding"</b>. 
/// Special-purpose direct bindings are represented by relationships named
/// <b>"material:binding:<i>purpose</i></b>. A direct binding relationship 
/// must have a single target path that points to a <b>UsdShadeMaterial</b>.</li>
/// <li>
/// In the case of a collection-based binding, the <i>allPurpose</i> binding is 
/// represented by a relationship named 
/// "material:binding:collection:<i>bindingName</i>", where 
/// <b>bindingName</b> establishes an identity for the binding that is unique 
/// on the prim. Attempting to establish two collection bindings of the same 
/// name on the same prim will result in the first binding simply being 
/// overridden. A special-purpose collection-based binding is represented by a 
/// relationship named "material:binding:collection:<i>purpose:bindingName</i>".
/// A collection-based binding relationship must have exacly two targets, one of 
/// which should be a collection-path (see 
/// ef UsdCollectionAPI::GetCollectionPath()) and the other should point to a
/// <b>UsdShadeMaterial</b>. In the future, we may allow a single collection 
/// binding to target multiple collections, if we can establish a reasonable 
/// round-tripping pattern for applications that only allow a single collection 
/// to be associated with each Material.
/// </li>
/// </ul>
/// 
/// <b>Note:</b> Both <b>bindingName</b> and <b>purpose</b> must be 
/// non-namespaced tokens. This allows us to know the role of a binding 
/// relationship simply from the number of tokens in it. 
/// <ul><li><b>Two tokens</b>: the fallback, "all purpose", direct binding, 
/// <i>material:binding</i></li>
/// <li><b>Three tokens</b>: a purpose-restricted, direct, fallback binding, 
/// e.g. material:binding:preview</li>
/// <li><b>Four tokens</b>: an all-purpose, collection-based binding, e.g. 
/// material:binding:collection:metalBits</li>
/// <li><b>Five tokens</b>: a purpose-restricted, collection-based binding, 
/// e.g. material:binding:collection:full:metalBits</li>
/// </ul>
/// 
/// A <b>binding-strength</b> value is used to specify whether a binding 
/// authored on a prim should be weaker or stronger than bindings that appear 
/// lower in namespace. We encode the binding strength with as token-valued 
/// metadata <b>'bindMaterialAs'</b> for future flexibility, even though for 
/// now, there are only two possible values:
/// <i>UsdShadeTokens->weakerThanDescendants</i> and 
/// <i>UsdShadeTokens->strongerThanDescendants</i>. When binding-strength is 
/// not authored (i.e. empty) on a binding-relationship, the default behavior 
/// matches UsdShadeTokens->weakerThanDescendants.
/// 
/// \note If a material binding relationship is a built-in property defined as 
/// part of a typed prim's schema, a fallback value should not be provided for 
/// it. This is because the "material resolution" algorithm only conisders 
/// <i>authored</i> properties.
/// 
///
class UsdShadeMaterialBindingAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdShadeMaterialBindingAPI on UsdPrim \p prim .
    /// Equivalent to UsdShadeMaterialBindingAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdShadeMaterialBindingAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdShadeMaterialBindingAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdShadeMaterialBindingAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdShadeMaterialBindingAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDSHADE_API
    virtual ~UsdShadeMaterialBindingAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDSHADE_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdShadeMaterialBindingAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdShadeMaterialBindingAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDSHADE_API
    static UsdShadeMaterialBindingAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);


    /// Returns true if this <b>single-apply</b> API schema can be applied to 
    /// the given \p prim. If this schema can not be a applied to the prim, 
    /// this returns false and, if provided, populates \p whyNot with the 
    /// reason it can not be applied.
    /// 
    /// Note that if CanApply returns false, that does not necessarily imply
    /// that calling Apply will fail. Callers are expected to call CanApply
    /// before calling Apply if they want to ensure that it is valid to 
    /// apply a schema.
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDSHADE_API
    static bool 
    CanApply(const UsdPrim &prim, std::string *whyNot=nullptr);

    /// Applies this <b>single-apply</b> API schema to the given \p prim.
    /// This information is stored by adding "MaterialBindingAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdShadeMaterialBindingAPI object is returned upon success. 
    /// An invalid (or empty) UsdShadeMaterialBindingAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for conditions 
    /// resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDSHADE_API
    static UsdShadeMaterialBindingAPI 
    Apply(const UsdPrim &prim);

protected:
    /// Returns the kind of schema this class belongs to.
    ///
    /// \sa UsdSchemaKind
    USDSHADE_API
    UsdSchemaKind _GetSchemaKind() const override;

private:
    // needs to invoke _GetStaticTfType.
    friend class UsdSchemaRegistry;
    USDSHADE_API
    static const TfType &_GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
    USDSHADE_API
    const TfType &_GetTfType() const override;

public:
    // ===================================================================== //
    // Feel free to add custom code below this line, it will be preserved by 
    // the code generator. 
    //
    // Just remember to: 
    //  - Close the class declaration with }; 
    //  - Close the namespace with PXR_NAMESPACE_CLOSE_SCOPE
    //  - Close the include guard with #endif
    // ===================================================================== //
    // --(BEGIN CUSTOM CODE)--

    /// \anchor UsdShadeMaterialBindingAPI_SchemaProperties
    /// \name Schema property and associated data retrieval API
    /// 
    /// This section contains API for fetching the two kinds of binding 
    /// relationships and for computing the corresponding bindings.
    /// 
    /// @{
        
    /// Returns the direct material-binding relationship on this prim for the 
    /// given material purpose.
    /// 
    /// The material purpose of the relationship that's returned will match 
    /// the specified \p materialPurpose.
    USDSHADE_API
    UsdRelationship GetDirectBindingRel(
        const TfToken &materialPurpose=UsdShadeTokens->allPurpose) const;
    
    /// Returns the collection-based material-binding relationship with the 
    /// given \p bindingName and \p materialPurpose on this prim.
    /// 
    /// For info on \p bindingName, see UsdShadeMaterialBindingAPI::Bind().
    /// The material purpose of the relationship that's returned will match 
    /// the specified \p materialPurpose.
    USDSHADE_API
    UsdRelationship GetCollectionBindingRel(
        const TfToken &bindingName,
        const TfToken &materialPurpose=UsdShadeTokens->allPurpose) const;

    /// Returns the list of collection-based material binding relationships
    /// on this prim for the given material purpose, \p materialPurpose.
    /// 
    /// The returned list of binding relationships will be in native property
    /// order. See UsdPrim::GetPropertyOrder(), UsdPrim::SetPropertyOrder(). 
    /// Bindings that appear earlier in the property order are considered to be 
    /// stronger than the ones that come later. See rule #6 in 
    /// \ref UsdShadeMaterialBindingAPI_MaterialResolution.
    USDSHADE_API
    std::vector<UsdRelationship> GetCollectionBindingRels(
        const TfToken &materialPurpose=UsdShadeTokens->allPurpose) const;

    /// \class DirectBinding
    /// This class represents a direct material binding.
    class DirectBinding {
    public:
        /// Default constructor initializes a DirectBinding object with 
        /// invalid material and bindingRel data members.
        DirectBinding() 
        {}

        USDSHADE_API
        explicit DirectBinding(const UsdRelationship &bindingRel);

        /// Gets the material object that this direct binding binds to.
        USDSHADE_API
        UsdShadeMaterial GetMaterial() const;

        /// Returns the path to the material that is bound to by this 
        /// direct binding.
        const SdfPath &GetMaterialPath() const {
            return _materialPath;
        }

        /// Returns the binding-relationship that represents this direct 
        /// binding.
        const UsdRelationship &GetBindingRel() const {
            return _bindingRel;
        }

        /// Returns the purpose of the direct binding.
        const TfToken &GetMaterialPurpose() const {
            return _materialPurpose;
        }

    private:
        // The path to the material that is bound to.
        SdfPath _materialPath; 
        
        // The binding relationship.
        UsdRelationship _bindingRel;

        // The purpose of the material binding.
        TfToken _materialPurpose;
    };

    /// \class CollectionBinding 
    /// This struct is used to represent a collection-based material binding,
    /// which contains two objects - a collection and a bound material.
    class CollectionBinding {
    public:
        /// Default constructor initializes a CollectionBinding object with 
        /// invalid collection, material and bindingRel data members.
        CollectionBinding() 
        {}

        /// Constructs a CollectionBinding object from the given collection-
        /// binding relationship. This inspects the targets of the relationship
        /// and determines the bound collection and the target material that 
        /// the collection is bound to. 
        USDSHADE_API
        explicit CollectionBinding(const UsdRelationship &collBindingRel);

        /// Constructs and returns the material object that this 
        /// collection-based binding binds to.
        USDSHADE_API
        UsdShadeMaterial GetMaterial() const;

        /// Constructs and returns the CollectionAPI object for the collection 
        /// that is bound by this collection-binding.
        USDSHADE_API
        UsdCollectionAPI GetCollection() const;
        
        /// Returns true if the CollectionBinding points to a valid material
        /// and collection.
        bool IsValid() const {
            return GetCollection() && GetMaterial();
        }
        /// Returns the path to the collection that is bound by this binding.
        const SdfPath &GetCollectionPath() const { 
            return _collectionPath;
        }

        /// Returns the path to the material that is bound to by this binding.
        const SdfPath &GetMaterialPath() const { 
            return _materialPath;
        }

        /// Returns the binding-relationship that represents this collection-
        /// based binding.
        const UsdRelationship &GetBindingRel() const {
            return _bindingRel;
        }

    private:
        // The collection being bound.
        SdfPath _collectionPath;

        // The material that is bound to. 
        SdfPath _materialPath;

        // The relationship that binds the collection to the material.
        UsdRelationship _bindingRel;
    };

    using CollectionBindingVector = std::vector<CollectionBinding>;

    /// Computes and returns the direct binding for the given material purpose 
    /// on this prim. 
    /// 
    /// The returned binding always has the specified \p materialPurpose
    /// (i.e. the all-purpose binding is not returned if a special purpose 
    /// binding is requested).
    ///
    /// If the direct binding is to a prim that is not a Material, this does not 
    /// generate an error, but the returned Material will be invalid (i.e. 
    /// evaluate to false).
    USDSHADE_API
    DirectBinding GetDirectBinding(
        const TfToken &materialPurpose=UsdShadeTokens->allPurpose) const;

    /// Returns all the collection-based bindings on this prim for the given
    /// material purpose.
    /// 
    /// The returned CollectionBinding objects always have the specified 
    /// \p materialPurpose (i.e. the all-purpose binding is not returned if a 
    /// special purpose binding is requested). 
    ///
    /// If one or more collection based bindings are to prims that are not 
    /// Materials, this does not generate an error, but the corresponding 
    /// Material(s) will be invalid (i.e. evaluate to false).
    /// 
    /// The python version of this API returns a tuple containing the 
    /// vector of CollectionBinding objects and the corresponding vector 
    /// of binding relationships.
    ///
    /// The returned list of collection-bindings will be in native property
    /// order of the associated binding relationships. See 
    /// UsdPrim::GetPropertyOrder(), UsdPrim::SetPropertyOrder(). 
    /// Binding relationships that come earlier in the list are considered to 
    /// be stronger than the ones that come later. See rule #6 in 
    /// \ref UsdShadeMaterialBindingAPI_MaterialResolution.
    USDSHADE_API
    CollectionBindingVector GetCollectionBindings(
        const TfToken &materialPurpose=UsdShadeTokens->allPurpose) const;
    
    /// Resolves the 'bindMaterialAs' token-valued metadata on the given binding 
    /// relationship and returns it.
    /// If the resolved value is empty, this returns the fallback value 
    /// UsdShadeTokens->weakerThanDescendants.
    /// 
    /// \sa UsdShadeMaterialBindingAPI::SetMaterialBindingStrength()
    USDSHADE_API
    static TfToken GetMaterialBindingStrength(
        const UsdRelationship &bindingRel);

    /// Sets the 'bindMaterialAs' token-valued metadata on the given binding 
    /// relationship.
    /// 
    /// If \p bindingStrength is <i>UsdShadeTokens->fallbackStrength</i>, the 
    /// value UsdShadeTokens->weakerThanDescendants is authored sparsely, i.e. 
    /// only when there is a different existing bindingStrength value.
    /// To stamp out the bindingStrength value explicitly, clients can pass in
    /// UsdShadeTokens->weakerThanDescendants or 
    /// UsdShadeTokens->strongerThanDescendants directly.

    /// Returns true on success, false otherwise.
    /// 
    /// \sa UsdShadeMaterialBindingAPI::GetMaterialBindingStrength()
    USDSHADE_API
    static bool SetMaterialBindingStrength(
        const UsdRelationship &bindingRel,
        const TfToken &bindingStrength);

    /// @}

    /// \anchor UsdShadeMaterialBindingAPI_Binding
    /// \name Binding authoring and clearing API 
    /// 
    /// This section provides API for authoring and clearing both direct and 
    /// collection-based material bindings on a prim.
    /// 
    /// @{

    /// Authors a direct binding to the given \p material on this prim.
    /// 
    /// If \p bindingStrength is UsdShadeTokens->fallbackStrength, the value 
    /// UsdShadeTokens->weakerThanDescendants is authored sparsely. 
    /// To stamp out the bindingStrength value explicitly, clients can pass in
    /// UsdShadeTokens->weakerThanDescendants or 
    /// UsdShadeTokens->strongerThanDescendants directly. 
    /// 
    /// If \p materialPurpose is specified and isn't equal to 
    /// UsdShadeTokens->allPurpose, the binding only applies to the specified 
    /// material purpose.
    ///
    /// Note that UsdShadeMaterialBindingAPI is a SingleAppliedAPI schema which 
    /// when applied updates the prim definition accordingly. This information 
    /// on the prim definition is helpful in multiple queries and more
    /// performant. Hence its recommended to call
    /// UsdShadeMaterialBindingAPI::Apply() when Binding a material.
    /// 
    /// Returns true on success, false otherwise.
    USDSHADE_API
    bool Bind(
        const UsdShadeMaterial &material,
        const TfToken &bindingStrength=UsdShadeTokens->fallbackStrength,
        const TfToken &materialPurpose=UsdShadeTokens->allPurpose) const;

    /// Authors a collection-based binding, which binds the given \p material 
    /// to the given \p collection on this prim.
    /// 
    /// \p bindingName establishes an identity for the binding that is unique 
    /// on the prim. Attempting to establish two collection bindings of the same 
    /// name on the same prim will result in the first binding simply being 
    /// overridden. If \p bindingName is empty, it is set to the base-name of 
    /// the collection being bound (which is the collection-name with any 
    /// namespaces stripped out). If there are multiple collections with the 
    /// same base-name being bound at the same prim, clients should pass in a 
    /// unique binding name per binding, in order to preserve all bindings. 
    /// The binding name used in constructing the collection-binding 
    /// relationship name shoud not contain namespaces. Hence, a coding error 
    /// is issued and no binding is authored if the provided value of 
    /// \p bindingName is non-empty and contains namespaces. 
    /// 
    /// If \p bindingStrength is <i>UsdShadeTokens->fallbackStrength</i>, the 
    /// value UsdShadeTokens->weakerThanDescendants is authored sparsely, i.e. 
    /// only when there is an existing binding with a different bindingStrength.
    /// To stamp out the bindingStrength value explicitly, clients can pass in
    /// UsdShadeTokens->weakerThanDescendants or 
    /// UsdShadeTokens->strongerThanDescendants directly.
    /// 
    /// If \p materialPurpose is specified and isn't equal to 
    /// UsdShadeTokens->allPurpose, the binding only applies to the specified 
    /// material purpose.
    ///
    /// Note that UsdShadeMaterialBindingAPI is a SingleAppliedAPI schema which 
    /// when applied updates the prim definition accordingly. This information 
    /// on the prim definition is helpful in multiple queries and more
    /// performant. Hence its recommended to call
    /// UsdShadeMaterialBindingAPI::Apply() when Binding a material.
    /// 
    /// Returns true on success, false otherwise.
    USDSHADE_API
    bool Bind(
        const UsdCollectionAPI &collection, 
        const UsdShadeMaterial &material,
        const TfToken &bindingName=TfToken(),
        const TfToken &bindingStrength=UsdShadeTokens->fallbackStrength,
        const TfToken &materialPurpose=UsdShadeTokens->allPurpose) const;

    /// Unbinds the direct binding for the given material purpose 
    /// (\p materialPurpose) on this prim. It accomplishes this by blocking
    /// the targets of the binding relationship in the current edit target.
    USDSHADE_API
    bool UnbindDirectBinding(
        const TfToken &materialPurpose=UsdShadeTokens->allPurpose) const;

    /// Unbinds the collection-based binding with the given \p bindingName, for 
    /// the given \p materialPurpose on this prim. It accomplishes this by 
    /// blocking the targets of the associated binding relationship in the 
    /// current edit target.
    USDSHADE_API
    bool UnbindCollectionBinding(
        const TfToken &bindingName, 
        const TfToken &materialPurpose=UsdShadeTokens->allPurpose) const;

    /// Unbinds all direct and collection-based bindings on this prim.
    USDSHADE_API
    bool UnbindAllBindings() const;

    /// Removes the specified \p prim from the collection targeted by the 
    /// binding relationship corresponding to given \p bindingName and 
    /// \p materialPurpose.
    /// 
    /// If the collection-binding relationship doesn't exist or if the
    /// targeted collection does not include the \p prim, then this does 
    /// nothing and returns true.
    /// 
    /// If the targeted collection includes \p prim, then this modifies the 
    /// collection by removing the prim from it (by invoking 
    /// UsdCollectionAPI::RemovePrim()). This method can be used in conjunction
    /// with the Unbind*() methods (if desired) to guarantee that a prim
    /// has no resolved material binding.
    USDSHADE_API
    bool RemovePrimFromBindingCollection(
        const UsdPrim &prim, 
        const TfToken &bindingName,
        const TfToken &materialPurpose) const;

    /// Adds the specified \p prim to the collection targeted by the 
    /// binding relationship corresponding to given \p bindingName and 
    /// \p materialPurpose.
    /// 
    /// If the collection-binding relationship doesn't exist or if the
    /// targeted collection already includes the \p prim, then this does 
    /// nothing and returns true.
    /// 
    /// If the targeted collection does not include \p prim (or excludes it 
    /// explicitly), then this modifies the collection by adding the prim to it 
    /// (by invoking UsdCollectionAPI::AddPrim()).
    USDSHADE_API
    bool AddPrimToBindingCollection(
        const UsdPrim &prim, 
        const TfToken &bindingName,
        const TfToken &materialPurpose) const;

    /// @}

    /// \anchor UsdShadeMaterialBindingAPI_MaterialResolution
    /// \name Bound Material Resolution
    /// 
    /// Material resolution is the process of determining the final bound 
    /// material for a given gprim (or UsdGeomSubset), for a given value of 
    /// material purpose. It involves examining all the bindings on the prim 
    /// and its ancestors, until a matching binding is found. The following 
    /// set of rules are applied in the process:
    /// <ul>
    /// <li>[1] Material bindings are inherited down the namespace chain. 
    /// Bindings lower in namespace (closer to leaf gprims) are stronger than 
    /// bindings on ancestors, unless they have their binding-strength set to 
    /// <i>UsdShadeTokens->strongerThanDescendants</i>.</li>
    /// <li>[2] A collection binding only applies to members of the collection 
    /// that are at or beneath the prim owning the binding relationship.</li>
    /// <li>[3] The purpose of the resolved material binding must either match 
    /// the requested special (i.e. restricted) purpose or be an all-purpose 
    /// binding. The restricted purpose binding, if available is preferred over 
    /// an all-purpose binding.
    /// <li>[4] At any given prim, the collection-based bindings are considered 
    /// to be stronger than the direct bindings. This reflects our belief that 
    /// the combination would appear primarily to define a "fallback" material 
    /// to be used by any child prims that are not targeted by a more specific 
    /// assignment</li>
    /// <li>[5] Collection-based binding relationships are applied in native 
    /// property order, with the earlier ordered binding relationships being 
    /// stronger.</li>
    /// <li>[6] The "namespace specificity" with which a prim is included in a
    /// collection is irrelevant to the binding strength of the collection. For 
    /// example, if a prim contains the ordered collection bindings 
    /// material:binding:collection:metalBits and 
    /// material:binding:collection:plasticBits, each of which targets a 
    /// collection of the same name, then if metalBits includes </Chair/Back>, 
    /// while plasticBits includes </Chair/Back/Brace/Rivet>, the binding for 
    /// </Chair/Back/Brace/Rivet> will be metalBits, because the metalBits 
    /// collection is bound more strongly than the plasticBits, and includes 
    /// an ancestor of </Chair/Back/Brace/Rivet>.
    /// </li> 
    /// </ul>
    /// 
    /// \note If a material binding relationship is a built-in property defined 
    /// as part of a typed prim schema, a fallback value should not be provided 
    /// for it. This is because the "material resolution" algorithm only 
    /// conisders <i>authored</i> properties.
    /// 
    /// @{

    /// An unordered list of collection paths mapped to the associated 
    /// collection's MembershipQuery object. This is used to cache the 
    /// MembershipQuery objects for collections that are encountered during 
    /// binding resolution for a tree of prims.
    using CollectionQueryCache = 
        tbb::concurrent_unordered_map<SdfPath, 
            std::unique_ptr<UsdCollectionAPI::MembershipQuery>, SdfPath::Hash>;

    /// Alias for a unique_ptr to a DirectBinding object.
    using DirectBindingPtr = std::unique_ptr<DirectBinding>;

    struct BindingsAtPrim {
        /// Inspects all the material:binding* properties on the \p prim and 
        /// computes direct and collection-based bindings for the given 
        /// value of \p materialPurpose.
        USDSHADE_API
        BindingsAtPrim(const UsdPrim &prim, const TfToken &materialPurpose);

        /// If the prim has a restricted purpose direct binding, then it is 
        /// stored here. If there is no restricted purpose binding on the prim, 
        /// then the all-purpose direct binding is stored.
        DirectBindingPtr directBinding;
         
        /// The ordered list of restricted-purpose collection bindings on the 
        /// prim.
        CollectionBindingVector restrictedPurposeCollBindings;

        /// The ordered list of all-purpose collection bindings on the prim.
        CollectionBindingVector allPurposeCollBindings;
    };

    /// BindingsAtPrim needs to invoke private _GetCollectionBindings().
    friend struct BindingsAtPrim;

    /// An unordered list of prim-paths mapped to the corresponding set of 
    /// bindings at the associated prim. This is used when computing resolved 
    /// bindings to avoid redundant computations for the shared ancestor 
    /// prims and to re-use the computed results for leaf prims.
    using BindingsCache = tbb::concurrent_unordered_map<SdfPath,
            std::unique_ptr<BindingsAtPrim>, SdfPath::Hash>;

    /// \overload
    /// Computes the resolved bound material for this prim, for the given 
    /// material purpose. 
    /// 
    /// This overload of ComputeBoundMaterial makes use of the BindingsCache 
    /// (\p bindingsCache) and CollectionQueryCache (\p collectionQueryCache) 
    /// that are passed in, to avoid redundant binding computations and 
    /// computations of MembershipQuery objects for collections. 
    /// It would be beneficial to make use of these when resolving bindings for 
    /// a tree of prims. These caches are populated lazily as more and more 
    /// bindings are resolved.
    /// 
    /// When the goal is to compute the bound material for a range (or list) of 
    /// prims, it is recommended to use this version of ComputeBoundMaterial(). 
    /// Here's how you could compute the bindings of a range of prims 
    /// efficiently in C++:
    /// 
    /// \code
    /// std::vector<std::pair<UsdPrim, UsdShadeMaterial> primBindings; 
    /// UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    /// UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;
    /// 
    /// for (auto prim : UsdPrimRange(rootPrim)) {
    ///     UsdShadeMaterial boundMaterial = 
    ///           UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial(
    ///           &bindingsCache, &collQueryCache);
    ///     if (boundMaterial) {
    ///         primBindings.emplace_back({prim, boundMaterial});
    ///     }
    /// }
    /// \endcode
    /// 
    /// If \p bindingRel is not null, then it is set to the "winning" binding
    /// relationship.
    ///
    /// See \ref UsdShadeMaterialBindingAPI_MaterialResolution "Bound Material Resolution"
    /// for details on the material resolution process.
    /// 
    /// The python version of this method returns a tuple containing the 
    /// bound material and the "winning" binding relationship.
    USDSHADE_API
    UsdShadeMaterial ComputeBoundMaterial(
        BindingsCache *bindingsCache,
        CollectionQueryCache *collectionQueryCache,
        const TfToken &materialPurpose=UsdShadeTokens->allPurpose,
        UsdRelationship *bindingRel=nullptr) const;

    /// \overload
    /// Computes the resolved bound material for this prim, for the given 
    /// material purpose. 
    /// 
    /// This overload does not utilize cached MembershipQuery object. However, 
    /// it only computes the MembershipQuery of every collection that bound 
    /// in the ancestor chain at most once. 
    /// 
    /// If \p bindingRel is not null, then it is set to the winning binding
    /// relationship.
    /// 
    /// See \ref UsdShadeMaterialBindingAPI_MaterialResolution "Bound Material Resolution"
    /// for details on the material resolution process.
    ///
    /// The python version of this method returns a tuple containing the 
    /// bound material and the "winning" binding relationship.
    USDSHADE_API
    UsdShadeMaterial ComputeBoundMaterial(
        const TfToken &materialPurpose=UsdShadeTokens->allPurpose,
        UsdRelationship *bindingRel=nullptr) const;

    /// Static API for efficiently and concurrently computing the resolved 
    /// material bindings for a vector of UsdPrims, \p prims for the 
    /// given \p materialPurpose.
    /// 
    /// The size of the returned vector always matches the size of the input 
    /// vector, \p prims. If a prim is not bound to any material, an invalid 
    /// or empty UsdShadeMaterial is returned at the index corresponding to it.
    /// 
    /// If the pointer \p bindingRels points to a valid vector, then it is 
    /// populated with the set of all "winning" binding relationships.
    /// 
    /// The python version of this method returns a tuple containing two lists -
    /// the bound materials and the corresponding "winning" binding 
    /// relationships.
    USDSHADE_API
    static std::vector<UsdShadeMaterial> ComputeBoundMaterials(
        const std::vector<UsdPrim> &prims, 
        const TfToken &materialPurpose=UsdShadeTokens->allPurpose,
        std::vector<UsdRelationship> *bindingRels=nullptr);

    /// @}
        
    // --------------------------------------------------------------------- //
    /// \anchor UsdShadeMaterialBindingAPI_Subsets
    /// \name Binding materials to subsets
    /// 
    /// API to create, access and query the presence of GeomSubsets that are 
    /// created for the purpose of binding materials.
    /// 
    /// \note GeomSubsets can only be created on valid UsdGeomImageable prims. 
    /// Hence, this API only works when the prim held by the MaterialBindingAPI
    /// schema object is an imageable prim.
    /// 
    /// \note Material bindings authored on GeomSubsets are honored by renderers
    /// only if their familyName is <b>UsdShadeTokens->materialBind</b>. This 
    /// allows robust interchange of subset bindings between multiple DCC apps.
    /// 
    /// \note The family type of the <i>materialBind</i> family of subsets 
    /// defaults to UsdGeomTokens->nonOverlapping. It can be set to 
    /// UsdGeomTokens->partition, using the API 
    /// SetMaterialBindSubsetsFamilyType(). It should never be set to 
    /// UsdGeomTokens->unrestricted, since it is invalid for a piece of 
    /// geometry to be bound to multiple materials.
    ///
    /// Here's some sample code that shows how to create "face" subsets and 
    /// and bind materials to them.
    /// \code
    /// // Get the imageable prim under which subsets must be created and
    /// // bound.
    /// UsdGeomImageable mesh = UsdGeomImageable::Get(stage,
    ///         SdfPath("/path/to/meshPrim");
    ///        
    /// // Get the materials to bind to.
    /// UsdShadeMaterial plastic = UsdShadeMaterial::Get(stage, 
    ///         SdfPath("/path/to/PlasticMaterial");
    /// UsdShadeMaterial metal = UsdShadeMaterial::Get(stage, 
    ///         SdfPath("/path/to/MetalMaterial");    
    ///
    /// VtIntArray plasticFaces, metalFaces;
    /// //.. populate faceIndices here.
    /// //.. 
    /// 
    /// UsdShadeMaterialBindingAPI meshBindingAPI(mesh.GetPrim());
    /// UsdGeomSubset plasticSubset = meshBindingAPI.CreateMaterialBindSubset(
    ///                 "plasticSubset", plasticFaces);
    /// UsdGeomSubset metalSubset = meshBindingAPI.CreateMaterialBindSubset( 
    ///                 "metalSubset", metalFaces);
    /// 
    /// // Bind materials to the created geom-subsets.               
    /// UsdShadeMaterialBindingAPI(pasticSubset.GetPrim()).Bind(plastic)
    /// UsdShadeMaterialBindingAPI(metalSubset.GetPrim()).Bind(metal)
    /// 
    /// \endcode
    /// @{

    /// Creates a GeomSubset named \p subsetName with element type, 
    /// \p elementType and familyName <b>materialBind<b> below this prim. 
    /// 
    /// If a GeomSubset named \p subsetName already exists, then its 
    /// "familyName" is updated to be UsdShadeTokens->materialBind and its 
    /// indices (at <i>default</i> timeCode) are updated with the provided 
    /// \p indices value before returning. 
    /// 
    /// This method forces the familyType of the "materialBind" family of 
    /// subsets to UsdGeomTokens->nonOverlapping if it's unset or explicitly set
    /// to UsdGeomTokens->unrestricted.
    /// 
    /// The default value \p elementType is UsdGeomTokens->face, as we expect 
    /// materials to be bound most often to subsets of faces on meshes.
    USDSHADE_API
    UsdGeomSubset CreateMaterialBindSubset(
        const TfToken &subsetName,
        const VtIntArray &indices,
        const TfToken &elementType=UsdGeomTokens->face);

    /// Returns all the existing GeomSubsets with 
    /// familyName=UsdShadeTokens->materialBind below this prim.
    USDSHADE_API
    std::vector<UsdGeomSubset> GetMaterialBindSubsets();
    
    /// Author the <i>familyType</i> of the "materialBind" family of GeomSubsets
    /// on this prim.
    /// 
    /// The default \p familyType is  <i>UsdGeomTokens->nonOverlapping<i>. It 
    /// can be set to <i>UsdGeomTokens->partition</i> to indicate that the 
    /// entire imageable prim is included in the union of all the "materialBind" 
    /// subsets. The family type should never be set to 
    /// UsdGeomTokens->unrestricted, since it is invalid for a single piece 
    /// of geometry (in this case, a subset) to be bound to more than one 
    /// material. Hence, a coding error is issued if \p familyType is 
    /// UsdGeomTokens->unrestricted.
    /// 
    /// \sa UsdGeomSubset::SetFamilyType
    USDSHADE_API
    bool SetMaterialBindSubsetsFamilyType(const TfToken &familyType);

    /// Returns the familyType of the family of "materialBind" GeomSubsets on 
    /// this prim.
    /// 
    /// By default, materialBind subsets have familyType="nonOverlapping", but
    /// they can also be tagged as a "partition", using 
    /// SetMaterialBindSubsetsFamilyType(). 
    /// 
    /// \sa UsdGeomSubset::GetFamilyNameAttr
    USDSHADE_API
    TfToken GetMaterialBindSubsetsFamilyType();

    /// Test whether a given \p name contains the "material:binding:" prefix
    ///
    USDSHADE_API
    static bool CanContainPropertyName(const TfToken &name);

    /// @}

private:

    UsdRelationship _CreateDirectBindingRel(
        const TfToken &materialPurpose) const;
    
    UsdRelationship _CreateCollectionBindingRel(
        const TfToken &bindingName,
        const TfToken &materialPurpose) const;

    // Helper method for getting collection bindings when the set of all 
    // collection binding relationship names for the required purpose is 
    // known.
    CollectionBindingVector _GetCollectionBindings(
        const TfTokenVector &collBindingPropertyNames) const;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
