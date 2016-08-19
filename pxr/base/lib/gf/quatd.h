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
////////////////////////////////////////////////////////////////////////
// This file is generated by a script.  Do not edit directly.  Edit the
// quat.template.h file to make changes.

#ifndef GF_QUATD_H
#define GF_QUATD_H

/// \file gf/quatd.h
/// \ingroup group_gf_LinearAlgebra

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/traits.h"

#include <boost/functional/hash.hpp>

#include <iosfwd>

template <>
struct GfIsGfQuat<class GfQuatd> { static const bool value = true; };

/// \class GfQuatd
/// \ingroup group_gf_LinearAlgebra
///
/// Basic type: a quaternion, a complex number with a real coefficient and
/// three imaginary coefficients, stored as a 3-vector.
///
class GfQuatd
{
  public:
    typedef double ScalarType;
    typedef GfVec3d ImaginaryType;

    /// Default constructor leaves the quaternion undefined.
    GfQuatd() {}

    /// Initialize the real coefficient to \p realVal and the imaginary
    /// coefficients to zero.
    ///
    /// Since quaternions typically must be normalized, reasonable values for
    /// \p realVal are -1, 0, or 1.  Other values are legal but are likely to
    /// be meaningless.
    ///
    explicit GfQuatd (double realVal) : _imaginary(0), _real(realVal) {}

    /// Initialize the real and imaginary coefficients.
    GfQuatd(double real, double i, double j, double k)
        : _imaginary(i, j, k), _real(real)
    {
    }

    /// Initialize the real and imaginary coefficients.
    GfQuatd(double real, const GfVec3d &imaginary)
        : _imaginary(imaginary), _real(real)
    {
    }

    /// Implicitly convert from GfQuatf.
    GfQuatd(class GfQuatf const &other);
    /// Implicitly convert from GfQuath.
    GfQuatd(class GfQuath const &other);

    /// Return the identity quaternion, with real coefficient 1 and an
    /// imaginary coefficients all zero.
    static GfQuatd GetIdentity() { return GfQuatd(1.0); }

    /// Return the real coefficient.
    double GetReal() const { return _real; }

    /// Set the real coefficient.
    void SetReal(double real) { _real = real; }

    /// Return the imaginary coefficient.
    const GfVec3d &GetImaginary() const { return _imaginary; }

    /// Set the imaginary coefficients.
    void SetImaginary(const GfVec3d &imaginary) {
        _imaginary = imaginary;
    }

    /// Set the imaginary coefficients.
    void SetImaginary(double i, double j, double k) {
        _imaginary.Set(i, j, k);
    }

    /// Return geometric length of this quaternion.
    double GetLength() const { return GfSqrt(_GetLengthSquared()); }

    /// length of this quaternion is smaller than \p eps, return the identity
    /// quaternion.
    GfQuatd
    GetNormalized(double eps = GF_MIN_VECTOR_LENGTH) const {
        GfQuatd ret(*this);
        ret.Normalize(eps);
        return ret;
    }

    /// Normalizes this quaternion in place to unit length, returning the
    /// length before normalization. If the length of this quaternion is
    /// smaller than \p eps, this sets the quaternion to identity.
    double Normalize(double eps = GF_MIN_VECTOR_LENGTH);

    /// Return this quaternion's conjugate, which is the quaternion with the
    /// same real coefficient and negated imaginary coefficients.
    GfQuatd GetConjugate() const {
        return GfQuatd(GetReal(), -GetImaginary());
    }

    /// Return this quaternion's inverse, or reciprocal.  This is the
    /// quaternion's conjugate divided by it's squared length.
    GfQuatd GetInverse() const {
        return GetConjugate() / _GetLengthSquared();
    }

    /// Hash.
    friend inline size_t hash_value(const GfQuatd &q) {
        size_t h = hash_value(q.GetReal());
        boost::hash_combine(h, q.GetImaginary());
        return h;
    }

    /// Component-wise negation.
    GfQuatd operator-() const {
        return GfQuatd(-GetReal(), -GetImaginary());
    }

    /// Component-wise quaternion equality test. The real and imaginary parts
    /// must match exactly for quaternions to be considered equal.
    bool operator==(const GfQuatd &q) const {
        return (GetReal() == q.GetReal() &&
                GetImaginary() == q.GetImaginary());
    }

    /// Component-wise quaternion inequality test. The real and imaginary
    /// parts must match exactly for quaternions to be considered equal.
    bool operator!=(const GfQuatd &q) const {
        return !(*this == q);
    }

    /// Post-multiply quaternion \p q into this quaternion.
    GfQuatd &operator *=(const GfQuatd &q);

    /// Multiply this quaternion's coefficients by \p s.
    GfQuatd &operator *=(double s) {
        _real *= s;
        _imaginary *= s;
        return *this;
    }

    /// Divide this quaternion's coefficients by \p s.
    GfQuatd &operator /=(double s) {
        _real /= s;
        _imaginary /= s;
        return *this;
    }

    /// Add quaternion \p q to this quaternion.
    GfQuatd &operator +=(const GfQuatd &q) {
        _real += q._real;
        _imaginary += q._imaginary;
        return *this;
    }

    /// Component-wise unary difference operator.
    GfQuatd &operator -=(const GfQuatd &q)  {
        _real -= q._real;
        _imaginary -= q._imaginary;
        return *this;
    }

    /// Component-wise binary sum operator.
    friend GfQuatd
    operator +(const GfQuatd &q1, const GfQuatd &q2) {
        return GfQuatd(q1) += q2;
    }

    /// Component-wise binary difference operator.
    friend GfQuatd
    operator -(const GfQuatd &q1, const GfQuatd &q2) {
        return GfQuatd(q1) -= q2;
    }

    /// Returns the product of quaternions \p q1 and \p q2.
    friend GfQuatd
    operator *(const GfQuatd &q1, const GfQuatd &q2) {
        return GfQuatd(q1) *= q2;
    }

    /// Returns the product of quaternion \p q and scalar \p s.
    friend GfQuatd
    operator *(const GfQuatd &q, double s) {
        return GfQuatd(q) *= s;
    }

    /// Returns the product of quaternion \p q and scalar \p s.
    friend GfQuatd
    operator *(double s, const GfQuatd &q) {
        return GfQuatd(q) *= s;
    }

    /// Returns the product of quaternion \p q and scalar 1 / \p s.
    friend GfQuatd
    operator /(const GfQuatd &q, double s) {
        return GfQuatd(q) /= s;
    }

  private:
    /// Imaginary part
    GfVec3d _imaginary;

    /// Real part
    double _real;

    /// Returns the square of the length
    double
    _GetLengthSquared() const {
        return _real * _real + GfDot(_imaginary, _imaginary);
    }
};

/// Spherically linearly interpolate between \p q0 and \p q1.
///
/// If the interpolant \p alpha is zero, then the result is \p q0, while
/// \p alpha of one yields \p q1.
GfQuatd
GfSlerp(double alpha, const GfQuatd& q0, const GfQuatd& q1);

GfQuatd
GfSlerp(const GfQuatd& q0, const GfQuatd& q1, double alpha);

/// Output a GfQuatd using the format (re, i, j, k)
/// \ingroup group_gf_DebuggingOutput
std::ostream& operator<<(std::ostream &, GfQuatd const &);

#endif // GF_QUATD_H
