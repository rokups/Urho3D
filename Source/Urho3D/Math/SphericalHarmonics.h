//
// Copyright (c) 2017-2020 the rbfx project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#pragma once

#include "../Math/Color.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"

namespace Urho3D
{

class Archive;

/// Spherical harmonics scalar coefficients, 3 bands.
struct SphericalHarmonics9
{
    /// Array of factors.
    static constexpr float factors[9] =
    {
        0.282095f,
        0.488603f,
        0.488603f,
        0.488603f,
        1.092548f,
        1.092548f,
        0.315392f,
        1.092548f,
        0.546274f
    };

    /// Array of cosines.
    static constexpr float cosines[9] =
    {
        1.0f,
        2.0f / 3.0f,
        2.0f / 3.0f,
        2.0f / 3.0f,
        0.25f,
        0.25f,
        0.25f,
        0.25f,
        0.25f
    };

    /// Construct default.
    SphericalHarmonics9() = default;

    /// Construct SH9 coefficients from given *normalized* direction.
    explicit SphericalHarmonics9(const Vector3& dir)
    {
        values_[0] = factors[0];

        values_[1] = factors[1] * dir.y_;
        values_[2] = factors[2] * dir.z_;
        values_[3] = factors[3] * dir.x_;

        values_[4] = factors[4] * dir.x_ * dir.y_;
        values_[5] = factors[5] * dir.y_ * dir.z_;
        values_[6] = factors[6] * (3.0f * dir.z_ * dir.z_ - 1.0f);
        values_[7] = factors[7] * dir.x_ * dir.z_;
        values_[8] = factors[8] * (dir.x_ * dir.x_ - dir.y_ * dir.y_);
    }

    /// Coefficients.
    float values_[9]{};

    /// Zero harmonics.
    static const SphericalHarmonics9 ZERO;
};

/// Spherical harmonics color coefficients, 3 bands. Use Vector3 instead of Color to save memory.
struct SphericalHarmonicsColor9
{
    /// Construct default.
    SphericalHarmonicsColor9() = default;

    /// Construct SH9 coefficients from given color.
    explicit SphericalHarmonicsColor9(const Vector3& color)
    {
        static const float factor = 1.0f / (SphericalHarmonics9::cosines[0] * SphericalHarmonics9::factors[0]);
        values_[0] = color * factor;
    }

    /// Construct SH9 coefficients from given color.
    explicit SphericalHarmonicsColor9(const Color& color) : SphericalHarmonicsColor9(color.ToVector3()) {}

    /// Construct SH9 coefficients from given *normalized* direction and color.
    SphericalHarmonicsColor9(const Vector3& dir, const Vector3& color)
    {
        const SphericalHarmonics9 sh9{ dir };
        for (unsigned i = 0; i < 9; ++i)
            values_[i] = color * sh9.values_[i];
    }

    /// Construct SH9 coefficients from given *normalized* direction and color.
    SphericalHarmonicsColor9(const Vector3& dir, const Color& color) : SphericalHarmonicsColor9(dir, color.ToVector3()) {}

    /// Evaluate at given direction.
    Vector3 Evaluate(const Vector3& dir) const
    {
        const SphericalHarmonics9 sh{ dir.Normalized() };

        Vector3 result;
        for (unsigned i = 0; i < 9; ++i)
            result += SphericalHarmonics9::cosines[i] * sh.values_[i] * values_[i];

        return result;
    }

    /// Evaluate average.
    Vector3 EvaluateAverage() const
    {
        Vector3 result;
        result += SphericalHarmonics9::cosines[0] * SphericalHarmonics9::factors[0] * values_[0];
        result -= SphericalHarmonics9::cosines[6] * SphericalHarmonics9::factors[6] * values_[6];
        return result;
    }

    /// Accumulate spherical harmonics (inplace).
    SphericalHarmonicsColor9& operator +=(const SphericalHarmonicsColor9& rhs)
    {
        for (unsigned i = 0; i < 9; ++i)
            values_[i] += rhs.values_[i];
        return *this;
    }

    /// Scale spherical harmonics (inplace).
    SphericalHarmonicsColor9& operator *=(float rhs)
    {
        for (unsigned i = 0; i < 9; ++i)
            values_[i] *= rhs;
        return *this;
    }

    /// Scale spherical harmonics.
    SphericalHarmonicsColor9 operator *(float rhs) const
    {
        SphericalHarmonicsColor9 result = *this;
        result *= rhs;
        return result;
    }

    /// Coefficients.
    Vector3 values_[9]{};

    /// Zero harmonics.
    static const SphericalHarmonicsColor9 ZERO;
};

/// Spherical harmonics, optimized for dot products.
struct SphericalHarmonicsDot9
{
    /// Construct default.
    SphericalHarmonicsDot9() = default;

    /// Construct from color in linear color space.
    explicit SphericalHarmonicsDot9(const Vector3& color)
    {
        Ar_.w_ = color.x_;
        Ag_.w_ = color.y_;
        Ab_.w_ = color.z_;
    }

    /// Construct from color in linear color space.
    explicit SphericalHarmonicsDot9(const Color& color) : SphericalHarmonicsDot9(color.ToVector3()) {}

    /// Construct from spherical harmonics.
    explicit SphericalHarmonicsDot9(SphericalHarmonicsColor9 sh)
    {
        // Precompute everything
        for (unsigned i = 0; i < 9; ++i)
            sh.values_[i] *= SphericalHarmonics9::cosines[i] * SphericalHarmonics9::factors[i];

        // Repack 6-th component (3 * z * z - 1)
        sh.values_[0] -= sh.values_[6];
        sh.values_[6] *= 3.0f;

        // (Nx, Ny, Nz, 1)
        Ar_ = Vector4(sh.values_[3].x_, sh.values_[1].x_, sh.values_[2].x_, sh.values_[0].x_);
        Ag_ = Vector4(sh.values_[3].y_, sh.values_[1].y_, sh.values_[2].y_, sh.values_[0].y_);
        Ab_ = Vector4(sh.values_[3].z_, sh.values_[1].z_, sh.values_[2].z_, sh.values_[0].z_);

        // (Nx*Ny, Ny*Nz, Nz*Nz, Nz*Nx)
        Br_ = Vector4(sh.values_[4].x_, sh.values_[5].x_, sh.values_[6].x_, sh.values_[7].x_);
        Bg_ = Vector4(sh.values_[4].y_, sh.values_[5].y_, sh.values_[6].y_, sh.values_[7].y_);
        Bb_ = Vector4(sh.values_[4].z_, sh.values_[5].z_, sh.values_[6].z_, sh.values_[7].z_);

        // Nx*Nx - Ny*Ny
        C_ = Vector4(sh.values_[8], 1.0f);
    }

    /// Evaluate at given direction.
    Vector3 Evaluate(const Vector3& dir) const
    {
        const Vector4 a{ dir, 1.0f };
        const Vector4 b{ dir.x_ * dir.y_, dir.y_ * dir.z_, dir.z_ * dir.z_, dir.z_ * dir.x_ };
        const float c = dir.x_ * dir.x_ - dir.y_ * dir.y_;

        Vector3 result;
        result.x_ = Ar_.DotProduct(a);
        result.y_ = Ag_.DotProduct(a);
        result.z_ = Ab_.DotProduct(a);
        result.x_ += Br_.DotProduct(b);
        result.y_ += Bg_.DotProduct(b);
        result.z_ += Bb_.DotProduct(b);
        result += static_cast<Vector3>(C_) * c;
        return result;
    }

    /// Evaluate average.
    Vector3 EvaluateAverage() const
    {
        return { Ar_.w_, Ag_.w_, Ab_.w_ };
    }

    /// Accumulate spherical harmonics (inplace).
    SphericalHarmonicsDot9& operator +=(const SphericalHarmonicsDot9& rhs)
    {
        Ar_ += rhs.Ar_;
        Ag_ += rhs.Ag_;
        Ab_ += rhs.Ab_;
        Br_ += rhs.Br_;
        Bg_ += rhs.Bg_;
        Bb_ += rhs.Bb_;
        C_ += rhs.C_;
        return *this;
    }

    /// Accumulate spherical harmonics with constant value.
    SphericalHarmonicsDot9& operator +=(const Vector3& rhs)
    {
        Ar_.w_ += rhs.x_;
        Ag_.w_ += rhs.y_;
        Ab_.w_ += rhs.z_;
        return *this;
    }

    /// Scale spherical harmonics (inplace).
    SphericalHarmonicsDot9& operator *=(float rhs)
    {
        Ar_ *= rhs;
        Ag_ *= rhs;
        Ab_ *= rhs;
        Br_ *= rhs;
        Bg_ *= rhs;
        Bb_ *= rhs;
        C_ *= rhs;
        return *this;
    }

    /// Scale spherical harmonics.
    SphericalHarmonicsDot9 operator *(float rhs) const
    {
        SphericalHarmonicsDot9 result = *this;
        result *= rhs;
        return result;
    }

    /// Return color for SH debug rendering.
    Color GetDebugColor() const
    {
        const Vector3 average = EvaluateAverage();
        return static_cast<Color>(average).LinearToGamma();
    }

    /// Dot product with (Nx, Ny, Nz, 1), red channel.
    Vector4 Ar_;
    /// Dot product with (Nx, Ny, Nz, 1), green channel.
    Vector4 Ag_;
    /// Dot product with (Nx, Ny, Nz, 1), blue channel.
    Vector4 Ab_;
    /// Dot product with (Nx*Ny, Ny*Nz, Nz*Nz, Nz*Nx), red channel.
    Vector4 Br_;
    /// Dot product with (Nx*Ny, Ny*Nz, Nz*Nz, Nz*Nx), green channel.
    Vector4 Bg_;
    /// Dot product with (Nx*Ny, Ny*Nz, Nz*Nz, Nz*Nx), blue channel.
    Vector4 Bb_;
    /// Color, multiplied by (Nx*Nx - Ny*Ny). W is always 1.0f.
    Vector4 C_;

    /// Zero harmonics.
    static const SphericalHarmonicsDot9 ZERO;
};

/// Serialize SH to archive.
URHO3D_API bool SerializeValue(Archive& archive, const char* name, SphericalHarmonicsDot9& value);

}
