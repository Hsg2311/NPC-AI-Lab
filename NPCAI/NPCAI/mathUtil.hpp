#ifndef __MathUtil_HPP
#define __MathUtil_HPP

#define __MathUtil_NOEXCEPT noexcept

#include <cmath>
#include <algorithm>
#include <concepts>
#include <cstdint>

#include <DirectXMath.h>

namespace mu {
namespace dx = DirectX;

inline constexpr auto pi = 3.1415926f;

class Degree;

class Radian {
public:
    constexpr Radian() __MathUtil_NOEXCEPT
        : Radian(0.f) {}

    constexpr Radian(float val) __MathUtil_NOEXCEPT
        : val_(val) {}

    constexpr Radian(Degree deg) __MathUtil_NOEXCEPT;

    Radian& operator+=(Radian rhs) __MathUtil_NOEXCEPT {
        val_ += rhs.val_;
        val_ = fmod(val_, 2 * pi);
        return *this;
    }

    Radian& operator-=(Radian rhs) __MathUtil_NOEXCEPT {
        val_ -= rhs.val_;
        val_ = fmod(val_, 2 * pi);
        return *this;
    }

    Radian& operator*=(Radian rhs) __MathUtil_NOEXCEPT {
        val_ *= rhs.val_;
        val_ = fmod(val_, 2 * pi);
        return *this;
    }

    Radian& operator/=(Radian rhs) __MathUtil_NOEXCEPT {
        val_ /= rhs.val_;
        val_ = fmod(val_, 2 * pi);
        return *this;
    }

    friend auto operator<=>(Radian lhs, Radian rhs) __MathUtil_NOEXCEPT = default;

    constexpr operator float() const __MathUtil_NOEXCEPT {
        return val_;
    }

private:
    float val_;
};

class Degree {
public:
    constexpr Degree() __MathUtil_NOEXCEPT
        : Degree(0.f) {}

    constexpr Degree(float val) __MathUtil_NOEXCEPT
        : val_(val) {}

    constexpr Degree(Radian rad) __MathUtil_NOEXCEPT
        : val_(static_cast<float>(rad) * 180.f / pi) {}

    Degree& operator+=(Degree rhs) __MathUtil_NOEXCEPT {
        val_ += rhs.val_;
        val_ = fmod(val_, 360.f);
        return *this;
    }

    Degree& operator-=(Degree rhs) __MathUtil_NOEXCEPT {
        val_ -= rhs.val_;
        val_ = fmod(val_, 360.f);
        return *this;
    }

    Degree& operator*=(Degree rhs) __MathUtil_NOEXCEPT {
        val_ *= rhs.val_;
        val_ = fmod(val_, 360.f);
        return *this;
    }

    Degree& operator/=(Degree rhs) __MathUtil_NOEXCEPT {
        val_ /= rhs.val_;
        val_ = fmod(val_, 360.f);
        return *this;
    }

    friend auto operator<=>(Degree lhs, Degree rhs) __MathUtil_NOEXCEPT = default;

    constexpr operator float() const __MathUtil_NOEXCEPT {
        return val_;
    }

private:
    float val_;
};

inline constexpr Radian::Radian(Degree deg) __MathUtil_NOEXCEPT
    : val_(static_cast<float>(deg) * pi / 180.f) {}

inline constexpr Radian operator""_rad(long double val) __MathUtil_NOEXCEPT {
    return Radian(static_cast<float>(val));
}

inline constexpr Degree operator""_deg(long double val) __MathUtil_NOEXCEPT {
    return Degree(static_cast<float>(val));
}

inline Radian operator+(Radian lhs, Radian rhs) __MathUtil_NOEXCEPT {
    return lhs += rhs;
}

inline Radian operator-(Radian lhs, Radian rhs) __MathUtil_NOEXCEPT {
    return lhs -= rhs;
}

inline Radian operator*(Radian lhs, Radian rhs) __MathUtil_NOEXCEPT {
    return lhs *= rhs;
}

inline Radian operator/(Radian lhs, Radian rhs) __MathUtil_NOEXCEPT {
    return lhs /= rhs;
}

inline Degree operator+(Degree lhs, Degree rhs) __MathUtil_NOEXCEPT {
    return lhs += rhs;
}

inline Degree operator-(Degree lhs, Degree rhs) __MathUtil_NOEXCEPT {
    return lhs -= rhs;
}

inline Degree operator*(Degree lhs, Degree rhs) __MathUtil_NOEXCEPT {
    return lhs *= rhs;
}

inline Degree operator/(Degree lhs, Degree rhs) __MathUtil_NOEXCEPT {
    return lhs /= rhs;
}

#define MU_CALLCONV XM_CALLCONV

template <std::size_t D>
    requires (D >= 1 && D <= 4)
class NVec;

template <std::size_t R, std::size_t C>
    requires (R >= 1 && R <= 4) && (C >= 1 && C <= 4)
class Mat;

template <std::size_t D /* Dimension */>
    requires (D >= 1 && D <= 4)
class alignas(16) Vec {
public:
    template <std::size_t D2>
        requires (D2 >= 1 && D2 <= 4)
    friend class Vec;

    template <std::size_t D2>
        requires (D2 >= 1 && D2 <= 4)
    friend class NVec;

    static Vec trueV() __MathUtil_NOEXCEPT {
        return Vec(dx::XMVectorTrueInt());
    }

    static Vec zero() __MathUtil_NOEXCEPT {
        return Vec(dx::XMVectorZero());
    }

    Vec() __MathUtil_NOEXCEPT
        : Vec(dx::XMVectorZero()) {}

    Vec(float val) __MathUtil_NOEXCEPT
        : Vec(dx::XMVectorReplicate(val)) {}

    Vec(float x, float y) __MathUtil_NOEXCEPT requires (D == 2)
        : Vec(dx::XMVectorSet(x, y, 0.f, 1.f)) {}

    Vec(float x, float y, float z) __MathUtil_NOEXCEPT requires (D == 3)
        : Vec(dx::XMVectorSet(x, y, z, 1.f)) {}

    Vec(float x, float y, float z, float w) __MathUtil_NOEXCEPT requires (D == 4)
        : Vec(dx::XMVectorSet(x, y, z, w)) {}

    Vec(dx::FXMVECTOR vec) __MathUtil_NOEXCEPT
        : vec_(vec) {}

    template <std::size_t D2>
    Vec(Vec<D2> vec) __MathUtil_NOEXCEPT
        : vec_(vec.vec_) {}

    template <std::size_t D2, std::floating_point ... Fs>
        requires (D2 >= 1 && D2 < 4 && D > D2)
    Vec(Vec<D2> vec, Fs... floats) __MathUtil_NOEXCEPT
        : vec_(vec.vec_) {
        if constexpr (D2 == 1) {
            const auto ctrl = dx::XMVectorSelectControl( 0, 1, 1, 1 );
            if constexpr (D == 2) {
                static_assert(sizeof...(floats) == 1, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, floats..., 0.f, 1.f), ctrl);
            }
            else if constexpr (D == 3) {
                static_assert(sizeof...(floats) == 2, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, floats..., 1.f), ctrl);
            }
            else {
                static_assert(sizeof...(floats) == 3, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, floats...), ctrl);
            }
        }
        else if constexpr (D2 == 2) {
            const auto ctrl = dx::XMVectorSelectControl( 0, 0, 1, 1 );
            if constexpr (D == 3) {
                static_assert(sizeof...(floats) == 1, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, 0.f, floats..., 1.f), ctrl);
            }
            else {
                static_assert(sizeof...(floats) == 2, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, 0.f, floats...), ctrl);
            }
        }
        else {
			const auto ctrl = dx::XMVectorSelectControl( 0, 0, 0, 1 );
            vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, 0.f, 0.f, floats...), ctrl);
        }
    }

    template <std::size_t D2, std::floating_point ... Fs>
        requires (D2 >= 1 && D2 < 4 && D > D2)
    Vec(NVec<D2> vec, Fs... floats) __MathUtil_NOEXCEPT;

    Vec& MU_CALLCONV operator+=(Vec rhs) __MathUtil_NOEXCEPT {
        using dx::operator+=;
        vec_ += rhs.vec_;
        return *this;
    }

    Vec& MU_CALLCONV operator-=(Vec rhs) __MathUtil_NOEXCEPT {
        using dx::operator-=;
        vec_ -= rhs.vec_;
        return *this;
    }

    Vec& MU_CALLCONV operator*=(Vec rhs) __MathUtil_NOEXCEPT {
        using dx::operator*=;
        vec_ *= rhs.vec_;
        return *this;
    }

    Vec& MU_CALLCONV operator/=(Vec rhs) __MathUtil_NOEXCEPT {
        using dx::operator/=;
        vec_ /= rhs.vec_;
        return *this;
    }

    Vec& MU_CALLCONV operator+=(float rhs) __MathUtil_NOEXCEPT {
        return *this += Vec(rhs);
    }

    Vec& MU_CALLCONV operator-=(float rhs) __MathUtil_NOEXCEPT {
        return *this -= Vec(rhs);
    }

    Vec& MU_CALLCONV operator*=(float rhs) __MathUtil_NOEXCEPT {
        return *this *= Vec(rhs);
    }

    Vec& MU_CALLCONV operator/=(float rhs) __MathUtil_NOEXCEPT {
        return *this /= Vec(rhs);
    }

    Vec& MU_CALLCONV operator+=(NVec<D> rhs) __MathUtil_NOEXCEPT;

    Vec& MU_CALLCONV operator-=(NVec<D> rhs) __MathUtil_NOEXCEPT;

    Vec& MU_CALLCONV operator*=(NVec<D> rhs) __MathUtil_NOEXCEPT;

    Vec& MU_CALLCONV operator/=(NVec<D> rhs) __MathUtil_NOEXCEPT;

    Vec& MU_CALLCONV operator*=(Mat<D, D> rhs) __MathUtil_NOEXCEPT;

    void setComponent(std::size_t idx, float value) __MathUtil_NOEXCEPT {
        assert(idx >= 0 && idx < D);
        vec_ = dx::XMVectorSetByIndex(vec_, value, idx);
    }

    dx::XMVECTOR& get() __MathUtil_NOEXCEPT {
        return vec_;
    }

    const dx::XMVECTOR& get() const __MathUtil_NOEXCEPT {
        return vec_;
    }

    float getXmf() const __MathUtil_NOEXCEPT requires (D == 1) {
        return dx::XMVectorGetX(vec_);
    }

    const dx::XMFLOAT2 getXmf() const __MathUtil_NOEXCEPT requires (D == 2) {
        dx::XMFLOAT2 ret;
        dx::XMStoreFloat2(&ret, vec_);
        return ret;
    }

    const dx::XMFLOAT3 getXmf() const __MathUtil_NOEXCEPT requires (D == 3) {
        dx::XMFLOAT3 ret;
        dx::XMStoreFloat3(&ret, vec_);
        return ret;
    }

    const dx::XMFLOAT4 getXmf() const __MathUtil_NOEXCEPT requires (D == 4) {
        dx::XMFLOAT4 ret;
        dx::XMStoreFloat4(&ret, vec_);
        return ret;
    }

    float MU_CALLCONV x() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetX(vec_);
    }

    float MU_CALLCONV y() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetY(vec_);
    }

    float MU_CALLCONV z() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetZ(vec_);
    }

    float MU_CALLCONV r() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetX(vec_);
    }

    float MU_CALLCONV g() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetY(vec_);
    }

    float MU_CALLCONV b() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetZ(vec_);
    }

    float MU_CALLCONV w() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetW(vec_);
    }

    float MU_CALLCONV operator[](std::size_t idx) const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetByIndex(vec_, idx);
    }

    float MU_CALLCONV len() const __MathUtil_NOEXCEPT {
        if constexpr (D == 1) {
            return dx::XMVectorGetX(vec_);
        } else if constexpr (D == 2) {
            return dx::XMVectorGetX(dx::XMVector2Length(vec_));
        } else if constexpr (D == 3) {
            return dx::XMVectorGetX(dx::XMVector3Length(vec_));
        } else if constexpr (D == 4) {
            return dx::XMVectorGetX(dx::XMVector4Length(vec_));
        }
    }

    Vec MU_CALLCONV lenV() const __MathUtil_NOEXCEPT {
        return Vec(dx::XMVector3Length(vec_));
    }

    float MU_CALLCONV len2() const __MathUtil_NOEXCEPT {
        if constexpr (D == 1) {
            return dx::XMVectorGetX(vec_) * dx::XMVectorGetX(vec_);
        } else if constexpr (D == 2) {
            return dx::XMVectorGetX(dx::XMVector2LengthSq(vec_));
        } else if constexpr (D == 3) {
            return dx::XMVectorGetX(dx::XMVector3LengthSq(vec_));
        } else if constexpr (D == 4) {
            return dx::XMVectorGetX(dx::XMVector4LengthSq(vec_));
        }
    }

    Vec MU_CALLCONV len2V() const __MathUtil_NOEXCEPT {
        return Vec(dx::XMVector3LengthSq(vec_));
    }

    float MU_CALLCONV norm() const __MathUtil_NOEXCEPT {
        return len2();
    }

    const Vec MU_CALLCONV operator-() const __MathUtil_NOEXCEPT {
        return Vec(dx::XMVectorNegate(vec_));
    }

    Vec<2> MU_CALLCONV xx() const __MathUtil_NOEXCEPT requires (D >= 2) {
        return Vec<2>(dx::XMVectorSwizzle<0, 0, 0, 0>(vec_));
    }

    Vec<2> MU_CALLCONV xy() const __MathUtil_NOEXCEPT requires (D >= 2) {
        return Vec<2>(dx::XMVectorSwizzle<0, 1, 0, 0>(vec_));
    }

    // TODO: 여기서부터 쭉 수정해야 함...
    Vec<2> MU_CALLCONV yx() const __MathUtil_NOEXCEPT requires (D >= 2) {
        return Vec<2>(dx::XMVectorSwizzle<1, 0>(vec_));
    }

    Vec<2> MU_CALLCONV yy() const __MathUtil_NOEXCEPT requires (D >= 2) {
        return Vec<2>(dx::XMVectorSwizzle<1, 1>(vec_));
    }

    Vec<3> MU_CALLCONV xxx() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<0, 0, 0>(vec_));
    }

    Vec<3> MU_CALLCONV xxy() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<0, 0, 1>(vec_));
    }

    Vec<3> MU_CALLCONV xxz() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<0, 0, 2>(vec_));
    }

    Vec<3> MU_CALLCONV xyx() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<0, 1, 0>(vec_));
    }

    Vec<3> MU_CALLCONV xyy() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<0, 1, 1>(vec_));
    }

    Vec<3> MU_CALLCONV xyz() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<0, 1, 2, 0>(vec_));
    }

    Vec<3> MU_CALLCONV xzx() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<0, 2, 0>(vec_));
    }

    Vec<3> MU_CALLCONV xzy() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<0, 2, 1>(vec_));
    }

    Vec<3> MU_CALLCONV xzz() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<0, 2, 2>(vec_));
    }

    Vec<3> MU_CALLCONV yxx() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<1, 0, 0>(vec_));
    }

    Vec<3> MU_CALLCONV yxy() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<1, 0, 1>(vec_));
    }

    Vec<3> MU_CALLCONV yxz() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<1, 0, 2>(vec_));
    }

    Vec<3> MU_CALLCONV yyx() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<1, 1, 0>(vec_));
    }

    Vec<3> MU_CALLCONV yyy() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<1, 1, 1>(vec_));
    }

    Vec<3> MU_CALLCONV yyz() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<1, 1, 2>(vec_));
    }

    Vec<3> MU_CALLCONV yzx() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<1, 2, 0>(vec_));
    }

    Vec<3> MU_CALLCONV yzy() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<1, 2, 1>(vec_));
    }

    Vec<3> MU_CALLCONV yzz() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<1, 2, 2>(vec_));
    }

    Vec<3> MU_CALLCONV zxx() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<2, 0, 0>(vec_));
    }

    Vec<3> MU_CALLCONV zxy() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<2, 0, 1>(vec_));
    }

    Vec<3> MU_CALLCONV zxz() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<2, 0, 2>(vec_));
    }

    Vec<3> MU_CALLCONV zyx() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<2, 1, 0>(vec_));
    }

    Vec<3> MU_CALLCONV zyy() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<2, 1, 1>(vec_));
    }

    Vec<3> MU_CALLCONV zyz() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<2, 1, 2>(vec_));
    }

    Vec<3> MU_CALLCONV zzx() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<2, 2, 0>(vec_));
    }

    Vec<3> MU_CALLCONV zzy() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<2, 2, 1>(vec_));
    }

    Vec<3> MU_CALLCONV zzz() const __MathUtil_NOEXCEPT requires (D >= 3) {
        return Vec<3>(dx::XMVectorSwizzle<2, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xxxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xxxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xxxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xxxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xxyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xxyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xxyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xxyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xxzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xxzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xxzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xxzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xxwx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xxwy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xxwz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xxww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 0, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xyxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xyxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xyxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xyxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xyyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xyyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xyyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xyyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xyzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xyzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xyzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xyzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xywx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xywy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xywz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xyww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 1, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xzxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 2, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xzxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 2, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xzxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 2, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xzxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 2, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xzyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 2, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xzyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 2, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xzyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 2, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xzyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 2, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xzzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 2, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xzzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 2, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xzzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 2, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xzzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 2, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xwxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xwxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xwxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xwxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xwyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xwyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xwyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xwyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xwzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xwzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xwzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xwzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV xwwx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV xwwy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV xwwz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV xwww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<0, 3, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV yxxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV yxxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV yxxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV yxxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV yxyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV yxyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV yxyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV yxyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV yxzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV yxzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV yxzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV yxzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV yxwx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV yxwy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV yxwz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV yxww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 0, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV yyxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV yyxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV yyxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV yyxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV yyyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV yyyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV yyyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV yyyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV yyzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV yyzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV yyzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV yyzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV yywx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV yywy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV yywz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV yyww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 1, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV yzxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV yzxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV yzxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV yzxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV yzyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV yzyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV yzyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV yzyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV yzzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV yzzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV yzzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV yzzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV yzwx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV yzwy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV yzwz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV yzww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 2, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV ywxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV ywxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV ywxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV ywxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV ywyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV ywyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV ywyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV ywyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV ywzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV ywzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV ywzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV ywzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV ywwx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV ywwy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV ywwz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV ywww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<1, 3, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zxxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zxxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zxxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zxxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zxyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zxyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zxyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zxyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zxzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zxzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zxzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zxzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zxwx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zxwy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zxwz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zxww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 0, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zyxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zyxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zyxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zyxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zyyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zyyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zyyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zyyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zyzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zyzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zyzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zyzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zywx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zywy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zywz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zyww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 1, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zzxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zzxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zzxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zzxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zzyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zzyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zzyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zzyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zzzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zzzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zzzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zzzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zzwx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zzwy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zzwz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zzww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 2, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zwxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zwxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zwxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zwxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zwyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zwyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zwyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zwyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zwzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zwzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zwzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zwzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV zwwx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV zwwy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV zwwz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV zwww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<2, 3, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wxxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wxxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wxxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wxxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wxyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wxyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wxyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wxyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wxzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wxzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wxzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wxzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wxwx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wxwy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wxwz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wxww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 0, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wyxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wyxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wyxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wyxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wyyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wyyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wyyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wyyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wyzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wyzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wyzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wyzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wywx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wywy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wywz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wyww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 1, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wzxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wzxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wzxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wzxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wzyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wzyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wzyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wzyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wzzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wzzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wzzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wzzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wzwx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wzwy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wzwz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wzww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 2, 3, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wwxx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 0, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wwxy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 0, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wwxz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 0, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wwxw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 0, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wwyx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 1, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wwyy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 1, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wwyz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 1, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wwyw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 1, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wwzx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 2, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wwzy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 2, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wwzz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 2, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wwzw() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 2, 3>(vec_));
    }

    Vec<4> MU_CALLCONV wwwx() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 3, 0>(vec_));
    }

    Vec<4> MU_CALLCONV wwwy() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 3, 1>(vec_));
    }

    Vec<4> MU_CALLCONV wwwz() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 3, 2>(vec_));
    }

    Vec<4> MU_CALLCONV wwww() const __MathUtil_NOEXCEPT requires (D >= 4) {
        return Vec<4>(dx::XMVectorSwizzle<3, 3, 3, 3>(vec_));
    }

private:
    dx::XMVECTOR vec_;
};

template <std::size_t D /* Dimension */>
    requires (D >= 1 && D <= 4)
class alignas(16) NVec {
public:
    struct NoNormalize_t {};

    template <std::size_t D2>
        requires (D2 >= 1 && D2 <= 4)
    friend class Vec;

    template <std::size_t D2>
        requires (D2 >= 1 && D2 <= 4)
    friend class NVec;

    NVec() __MathUtil_NOEXCEPT
        : NVec(1.f) {
        normalize();
    }

    NVec(float val) __MathUtil_NOEXCEPT
        : NVec(val, NoNormalize_t{}) {
        normalize();
    }

    NVec(float val, NoNormalize_t) __MathUtil_NOEXCEPT
        : vec_(dx::XMVectorReplicate(val)) {}

    NVec(float x, float y) __MathUtil_NOEXCEPT requires (D == 2)
        : NVec(x, y, NoNormalize_t{}) {
        normalize();
    }

    NVec(float x, float y, NoNormalize_t) __MathUtil_NOEXCEPT requires (D == 2)
        : vec_(dx::XMVectorSet(x, y, 0.f, 1.f)) {}

    NVec(float x, float y, float z) __MathUtil_NOEXCEPT requires (D == 3)
        : NVec(x, y, z, NoNormalize_t{}) {
        normalize();
    }

    NVec(float x, float y, float z, NoNormalize_t) __MathUtil_NOEXCEPT requires (D == 3)
        : vec_(dx::XMVectorSet(x, y, z, 1.f)) {}

    NVec(float x, float y, float z, float w) __MathUtil_NOEXCEPT requires (D == 4)
        : NVec(x, y, z, w, NoNormalize_t{}) {
        normalize();
    }
    
    NVec(float x, float y, float z, float w, NoNormalize_t) __MathUtil_NOEXCEPT requires (D == 4)
        : vec_(dx::XMVectorSet(x, y, z, w)) {}

    NVec(dx::FXMVECTOR vec) __MathUtil_NOEXCEPT
        : vec_(vec) {
        normalize();
    }

    template <std::size_t D2>
        requires (D2 >= 1 && D2 <= 4)
    NVec(Vec<D2> vec) __MathUtil_NOEXCEPT
        : NVec(vec, NoNormalize_t{}){
        normalize();
    }

    template <std::size_t D2>
        requires (D2 >= 1 && D2 <= 4)
    NVec(Vec<D2> vec, NoNormalize_t) __MathUtil_NOEXCEPT
        : vec_(vec.vec_) {}

    template <std::size_t D2, std::floating_point ... Fs>
        requires (D2 >= 1 && D2 < 4 && D > D2)
    NVec(Vec<D2> vec, Fs... floats) __MathUtil_NOEXCEPT
        : vec_(vec.vec_) {
        if constexpr (D2 == 1) {
            const auto ctrl = dx::XMVectorSet(0.f, 1.f, 1.f, 1.f);
            if constexpr (D == 2) {
                static_assert(sizeof...(floats) == 1, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, floats..., 0.f, 1.f), ctrl);
            }
            else if constexpr (D == 3) {
                static_assert(sizeof...(floats) == 2, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, floats..., 1.f), ctrl);
            }
            else {
                static_assert(sizeof...(floats) == 3, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, floats...), ctrl);
            }
        }
        else if constexpr (D2 == 2) {
            const auto ctrl = dx::XMVectorSet(0.f, 0.f, 1.f, 1.f);
            if constexpr (D == 3) {
                static_assert(sizeof...(floats) == 1, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, 0.f, floats..., 1.f), ctrl);
            }
            else {
                static_assert(sizeof...(floats) == 2, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, 0.f, floats...), ctrl);
            }
        }
        else {
            const auto ctrl = dx::XMVectorSet(0.f, 0.f, 0.f, 1.f);
            vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, 0.f, 0.f, floats...), ctrl);
        }

        normalize();
    }

    template <std::size_t D2, std::floating_point ... Fs>
        requires (D2 >= 1 && D2 < 4 && D > D2)
    NVec(NVec<D2> vec, Fs... floats) __MathUtil_NOEXCEPT
        : vec_(vec.vec_) {
        if constexpr (D2 == 1) {
            const auto ctrl = dx::XMVectorSet(0.f, 1.f, 1.f, 1.f);
            if constexpr (D == 2) {
                static_assert(sizeof...(floats) == 1, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, floats..., 0.f, 1.f), ctrl);
            }
            else if constexpr (D == 3) {
                static_assert(sizeof...(floats) == 2, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, floats..., 1.f), ctrl);
            }
            else {
                static_assert(sizeof...(floats) == 3, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, floats...), ctrl);
            }
        }
        else if constexpr (D2 == 2) {
            const auto ctrl = dx::XMVectorSet(0.f, 0.f, 1.f, 1.f);
            if constexpr (D == 3) {
                static_assert(sizeof...(floats) == 1, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, 0.f, floats..., 1.f), ctrl);
            }
            else {
                static_assert(sizeof...(floats) == 2, "Invalid number of arguments detected.");
                vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, 0.f, floats...), ctrl);
            }
        }
        else {
            const auto ctrl = dx::XMVectorSet(0.f, 0.f, 0.f, 1.f);
            vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, 0.f, 0.f, floats...), ctrl);
        }
    }

    dx::XMVECTOR& get() __MathUtil_NOEXCEPT {
        return vec_;
    }

    const dx::XMVECTOR& get() const __MathUtil_NOEXCEPT {
        return vec_;
    }

    float getXmf() const __MathUtil_NOEXCEPT requires (D == 1) {
        return dx::XMVectorGetX(vec_);
    }

    const dx::XMFLOAT2 getXmf() const __MathUtil_NOEXCEPT requires (D == 2) {
        dx::XMFLOAT2 ret;
        dx::XMStoreFloat3(&ret, vec_);
        return ret;
    }

    const dx::XMFLOAT3 getXmf() const __MathUtil_NOEXCEPT requires (D == 3) {
        dx::XMFLOAT3 ret;
        dx::XMStoreFloat3(&ret, vec_);
        return ret;
    }

    const dx::XMFLOAT4 getXmf() const __MathUtil_NOEXCEPT requires (D == 4) {
        dx::XMFLOAT4 ret;
        dx::XMStoreFloat4(&ret, vec_);
        return ret;
    }

    float MU_CALLCONV x() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetX(vec_);
    }

    float MU_CALLCONV y() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetY(vec_);
    }

    float MU_CALLCONV z() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetZ(vec_);
    }

    float MU_CALLCONV r() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetX(vec_);
    }

    float MU_CALLCONV g() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetY(vec_);
    }

    float MU_CALLCONV b() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetZ(vec_);
    }

    float MU_CALLCONV w() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetW(vec_);
    }

    float MU_CALLCONV operator[](std::size_t idx) const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetByIndex(vec_, idx);
    }

    constexpr float MU_CALLCONV len() const __MathUtil_NOEXCEPT {
        return 1.f;
    }

    constexpr float MU_CALLCONV len2() const __MathUtil_NOEXCEPT {
        return 1.f;
    }

    constexpr float MU_CALLCONV norm() const __MathUtil_NOEXCEPT {
        return 1.f;
    }

    template <std::size_t D2>
        requires (D2 >= 1 && D2 <= 4)
    operator Vec<D2>() const __MathUtil_NOEXCEPT {
        return Vec<D2>(vec_);
    }

    const Vec<D> MU_CALLCONV operator-() const __MathUtil_NOEXCEPT {
        return Vec<D>(dx::XMVectorNegate(vec_));
    }

private:
    void normalize() __MathUtil_NOEXCEPT {
        if constexpr (D == 1) {
            vec_ = dx::XMVectorReplicate(1.f);
        } else if constexpr (D == 2) {
            vec_ = dx::XMVector2Normalize(vec_);
        } else if constexpr (D == 3) {
            vec_ = dx::XMVector3Normalize(vec_);
        } else if constexpr (D == 4) {
            vec_ = dx::XMVector4Normalize(vec_);
        }
    }

    dx::XMVECTOR vec_;
};

template <std::size_t D>
    requires (D >= 1 && D <= 4)
template <std::size_t D2, std::floating_point ... Fs>
    requires (D2 >= 1 && D2 < 4 && D > D2)
Vec<D>::Vec(NVec<D2> vec, Fs... floats) __MathUtil_NOEXCEPT
    : vec_(vec.vec_) {
    if constexpr (D2 == 1) {
        const auto ctrl = dx::XMVectorSet(0.f, 1.f, 1.f, 1.f);
        if constexpr (D == 2) {
            static_assert(sizeof...(floats) == 1, "Invalid number of arguments detected.");
            vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, floats..., 0.f, 1.f), ctrl);
        }
        else if constexpr (D == 3) {
            static_assert(sizeof...(floats) == 2, "Invalid number of arguments detected.");
            vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, floats..., 1.f), ctrl);
        }
        else {
            static_assert(sizeof...(floats) == 3, "Invalid number of arguments detected.");
            vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, floats...), ctrl);
        }
    }
    else if constexpr (D2 == 2) {
        const auto ctrl = dx::XMVectorSet(0.f, 0.f, 1.f, 1.f);
        if constexpr (D == 3) {
            static_assert(sizeof...(floats) == 1, "Invalid number of arguments detected.");
            vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, 0.f, floats..., 1.f), ctrl);
        }
        else {
            static_assert(sizeof...(floats) == 2, "Invalid number of arguments detected.");
            vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, 0.f, floats...), ctrl);
        }
    }
    else {
        const auto ctrl = dx::XMVectorSet(0.f, 0.f, 0.f, 1.f);
        vec_ = dx::XMVectorSelect(vec_, dx::XMVectorSet(0.f, 0.f, 0.f, floats...), ctrl);
    }
}

template <std::size_t D> requires (D >= 1 && D <= 4)
Vec<D>& MU_CALLCONV Vec<D>::operator+=(NVec<D> rhs) __MathUtil_NOEXCEPT {
    using dx::operator+=;
    vec_ += rhs.vec_;
    return *this;
}

template <std::size_t D> requires (D >= 1 && D <= 4)
Vec<D>& MU_CALLCONV Vec<D>::operator-=(NVec<D> rhs) __MathUtil_NOEXCEPT {
    using dx::operator-=;
    vec_ -= rhs.vec_;
    return *this;
}

template <std::size_t D> requires (D >= 1 && D <= 4)
Vec<D>& MU_CALLCONV Vec<D>::operator*=(NVec<D> rhs) __MathUtil_NOEXCEPT {
    using dx::operator*=;
    vec_ *= rhs.vec_;
    return *this;
}

template <std::size_t D> requires (D >= 1 && D <= 4)
Vec<D>& MU_CALLCONV Vec<D>::operator/=(NVec<D> rhs) __MathUtil_NOEXCEPT {
    using dx::operator/=;
    vec_ /= rhs.vec_;
    return *this;
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator+(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorAdd(lhs.get(), rhs.get());
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator-(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorSubtract(lhs.get(), rhs.get());
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator*(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorMultiply(lhs.get(), rhs.get());
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator/(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorDivide(lhs.get(), rhs.get());
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator+(Vec<D> lhs, float rhs) __MathUtil_NOEXCEPT {
    return lhs += Vec<D>(rhs);
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator-(Vec<D> lhs, float rhs) __MathUtil_NOEXCEPT {
    return lhs -= Vec<D>(rhs);
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator*(Vec<D> lhs, float rhs) __MathUtil_NOEXCEPT {
    return lhs *= Vec<D>(rhs);
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator*(float lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return rhs * lhs;
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator/(Vec<D> lhs, float rhs) __MathUtil_NOEXCEPT {
    return lhs /= Vec<D>(rhs);
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator+(Vec<D> lhs, NVec<D> rhs) __MathUtil_NOEXCEPT {
    return lhs += rhs;
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator-(Vec<D> lhs, NVec<D> rhs) __MathUtil_NOEXCEPT {
    return lhs -= rhs;
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator*(Vec<D> lhs, NVec<D> rhs) __MathUtil_NOEXCEPT {
    return lhs *= rhs;
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator/(Vec<D> lhs, NVec<D> rhs) __MathUtil_NOEXCEPT {
    return lhs /= rhs;
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator+(NVec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return rhs + lhs;
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator-(NVec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return rhs - lhs;
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator*(NVec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return rhs * lhs;
}

template <std::size_t D>
const Vec<D> MU_CALLCONV operator/(NVec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return rhs / lhs;
}

template <std::size_t D>
bool MU_CALLCONV operator==(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    if constexpr (D == 1) {
        return dx::XMVectorGetX(lhs.get()) == dx::XMVectorGetX(rhs.get());
    } else if constexpr (D == 2) {
        return dx::XMVector2Equal(lhs.get(), rhs.get());
    } else if constexpr (D == 3) {
        return dx::XMVector3Equal(lhs.get(), rhs.get());
    } else if constexpr (D == 4) {
        return dx::XMVector4Equal(lhs.get(), rhs.get());
    }
}

template <std::size_t D>
bool MU_CALLCONV nearEqual(Vec<D> lhs, Vec<D> rhs, Vec<D> epsilon) __MathUtil_NOEXCEPT {
    if constexpr (D == 1) {
        return dx::XMScalarNearEqual(
            dx::XMVectorGetX(lhs.get()), dx::XMVectorGetX(rhs.get()),
            dx::XMVectorGetX(epsilon.get())
        );
    } else if constexpr (D == 2) {
        return dx::XMVector2NearEqual(lhs.get(), rhs.get(), epsilon.get());
    } else if constexpr (D == 3) {
        return dx::XMVector3NearEqual(lhs.get(), rhs.get(), epsilon.get());
    } else if constexpr (D == 4) {
        return dx::XMVector4NearEqual(lhs.get(), rhs.get(), epsilon.get());
    }
}

template <std::size_t D>
bool MU_CALLCONV nearEqual(Vec<D> lhs, Vec<D> rhs, float epsilon = dx::g_XMEpsilon) __MathUtil_NOEXCEPT {
    return nearEqual(lhs, rhs, Vec<D>(epsilon));
}


template <std::size_t D>
bool MU_CALLCONV operator!=(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return !(lhs == rhs);
}

template <std::size_t D1, std::size_t D2>
auto MU_CALLCONV operator<=>(Vec<D1> lhs, Vec<D2> rhs) __MathUtil_NOEXCEPT {
    return lhs.norm() <=> rhs.norm();
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
float MU_CALLCONV dot(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    if constexpr (D == 1) {
        return dx::XMVectorGetX(dx::XMVectorMultiply(lhs.get(), rhs.get()));
    } else if constexpr (D == 2) {
        return dx::XMVectorGetX(dx::XMVector2Dot(lhs.get(), rhs.get()));
    } else if constexpr (D == 3) {
        return dx::XMVectorGetX(dx::XMVector3Dot(lhs.get(), rhs.get()));
    } else if constexpr (D == 4) {
        return dx::XMVectorGetX(dx::XMVector4Dot(lhs.get(), rhs.get()));
    }
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
float MU_CALLCONV dot(NVec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dot(Vec<D>(lhs), rhs);
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
float MU_CALLCONV dot(Vec<D> lhs, NVec<D> rhs) __MathUtil_NOEXCEPT {
    return dot(lhs, Vec<D>(rhs));
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
float MU_CALLCONV dot(NVec<D> lhs, NVec<D> rhs) __MathUtil_NOEXCEPT {
    return dot(Vec<D>(lhs), Vec<D>(rhs));
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV cross(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    if constexpr (D == 2) {
        return dx::XMVector2Cross(lhs.get(), rhs.get());
    } else if constexpr (D == 3) {
        return dx::XMVector3Cross(lhs.get(), rhs.get());
    } else {
        static_assert(D == 2 || D == 3,
            "Cross product is only defined for 2D and 3D vectors."
        );
    }
}

template <std::uint32_t... Integers>
    requires (sizeof...(Integers) == 2)
const Vec<2> MU_CALLCONV permute(Vec<2> lhs, Vec<2> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorPermute<Integers..., 2, 3>(lhs.get(), rhs.get());
}

template <std::uint32_t... Integers>
    requires (sizeof...(Integers) == 3)
const Vec<3> MU_CALLCONV permute(Vec<3> lhs, Vec<3> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorPermute<Integers..., 3>(lhs.get(), rhs.get());
}

template <std::uint32_t... Integers>
    requires (sizeof...(Integers) == 4)
const Vec<4> MU_CALLCONV permute(Vec<4> lhs, Vec<4> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorPermute<Integers...>(lhs.get(), rhs.get());
}


template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV cross(NVec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return cross(Vec<D>(lhs), rhs);
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV cross(Vec<D> lhs, NVec<D> rhs) __MathUtil_NOEXCEPT {
    return cross(lhs, Vec<D>(rhs));
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const NVec<D> MU_CALLCONV cross(NVec<D> lhs, NVec<D> rhs) __MathUtil_NOEXCEPT {
    return NVec<D>(cross(Vec<D>(lhs), Vec<D>(rhs)), typename NVec<D>::NoNormalize_t{});
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const NVec<D> MU_CALLCONV normalize(Vec<D> vec) __MathUtil_NOEXCEPT {
    return NVec<D>(vec);
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const NVec<D> MU_CALLCONV normalizeEst(Vec<D> vec) __MathUtil_NOEXCEPT {
    return NVec<D>(dx::XMVector3NormalizeEst(vec.get()));
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV abs(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorAbs(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV floor(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorFloor(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV ceil(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorCeiling(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV round(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorRound(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV trunc(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorTruncate(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV frac(Vec<D> vec) __MathUtil_NOEXCEPT {
    return vec - trunc(vec);
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV select(
    Vec<D> lhs, Vec<D> rhs, Vec<D> control
) __MathUtil_NOEXCEPT {
    return dx::XMVectorSelect(lhs.get(), rhs.get(), control.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV sqrt(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorSqrt(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV sqrtEst(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorSqrtEst(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV rsqrt(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorReciprocalSqrt(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV reflect(
    Vec<D> vec, NVec<D> normal
) __MathUtil_NOEXCEPT {
    return vec - 2.f * dot(vec, normal) * normal;
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV refract(
    Vec<D> vec, NVec<D> normal, float eta
) __MathUtil_NOEXCEPT {
    auto dotVN = dot(vec, normal);
    auto k = 1.f - eta * eta * (1.f - dotVN * dotVN);
    if (k < 0.f) {
        return Vec<D>(0.f);
    } else {
        return eta * vec - (eta * dotVN + sqrt(k)) * normal;
    }
}

template <std::size_t D>
    requires (D >= 2 && D <= 3)
const Vec<D> MU_CALLCONV slide(
    Vec<D> vec, NVec<D> normal
) __MathUtil_NOEXCEPT {
    return vec - dot(vec, normal) * mu::Vec<D>(normal);
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV lerp(
    Vec<D> lhs, Vec<D> rhs, float t
) __MathUtil_NOEXCEPT {
    return lhs + t * (rhs - lhs);
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV saturate(
    Vec<D> vec
) __MathUtil_NOEXCEPT {
    return dx::XMVectorSaturate(vec.get());
}

// TODO: smoothstep, fade, step, boxstep, smoothmin, smoothmax, smoothminmax, etc.

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV barycentric(
    Vec<D> v1, Vec<D> v2, Vec<D> v3, float f, float g
) __MathUtil_NOEXCEPT {
    return dx::XMVectorBaryCentric(v1.get(), v2.get(), v3.get(), f, g);
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV catmullrom(
    Vec<D> v0, Vec<D> v1, Vec<D> v2, Vec<D> v3, float s
) __MathUtil_NOEXCEPT {
    return dx::XMVectorCatmullRom(v0.get(), v1.get(), v2.get(), v3.get(), s);
}

template <std::size_t D>
    requires (D >= 2 && D <= 3)
bool intersectLineSegments(
    Vec<D> a1, Vec<D> a2,
    Vec<D> b1, Vec<D> b2,
    Vec<D>& ip, float* pt = nullptr, float* pu = nullptr
) __MathUtil_NOEXCEPT {
    const auto d1 = a2 - a1;
    const auto d2 = b2 - b1;

    const auto n = cross(d1, d2);
    const auto det = n.len2();

    // Check if lines are parallel
    if (det < 1e-5f) {
        return false;
    }

    const auto r = b1 - a1;
    const auto t = dot(cross(r, d2), n) / det;

    // Check if lines are collinear
    if (t < 0.f || t > 1.f) {
        return false;
    }

    const auto u = dot(cross(r, d1), n) / det;
    if (u < 0.f || u > 1.f) {
        return false;
    }

    // Lines intersect
    ip = a1 + t * d1;

    if (pt) {
        *pt = t;
    }

    if (pu) {
        *pu = u;
    }

    return true;
}

template <std::size_t D>
    requires (D == 3)
bool intersectLineAndPlane(
    Vec<D> a1, Vec<D> a2,
    NVec<D> N, float d, Vec<D>& ip, float* pt = nullptr
) __MathUtil_NOEXCEPT {
    const NVec<D> v = a2 - a1;

    auto denom = dot(N, v);

    if (std::abs(denom) < 1e-5f) {
        return false; // Line is parallel to plane
    }

    auto t = -(d + dot(N, a1)) / denom;

    // Line segment intersects plane
    ip = a1 + t * mu::Vec<D>(v);

    if (pt) {
        *pt = t;
    }

    return true;
}

template <std::size_t D>
    requires (D == 3)
bool intersectLineSegmentAndPlane(
    Vec<D> a1, Vec<D> a2,
    NVec<D> N, float d, Vec<D>& ip, float* pt = nullptr
) __MathUtil_NOEXCEPT {
    const NVec<D> v = a2 - a1;

    auto denom = dot(N, v);

    if (std::abs(denom) < 1e-5f) {
        return false; // Line is parallel to plane
    }

    auto t = -(d + dot(N, a1)) / denom;
    if (t < 0.f || t > 1.f) {
        return false; // Line segment does not intersect plane
    }

    // Line segment intersects plane
    ip = a1 + t * mu::Vec<D>(v);

    if (pt) {
        *pt = t;
    }

    return true;
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV hermite(
    Vec<D> v1, Vec<D> t1, Vec<D> v2, Vec<D> t2, float s
) __MathUtil_NOEXCEPT {
    return dx::XMVectorHermite(v1.get(), t1.get(), v2.get(), t2.get(), s);
}

template <std::size_t D1, std::size_t D2>
const Vec<std::max(D1, D2)> MU_CALLCONV min(
    Vec<D1> lhs, Vec<D2> rhs
) __MathUtil_NOEXCEPT {
    return dx::XMVectorMin(lhs.get(), rhs.get());
}

template <std::size_t D>
const Vec<D> MU_CALLCONV max(
    Vec<D> lhs, Vec<D> rhs
) __MathUtil_NOEXCEPT {
    return dx::XMVectorMax(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV clamp(
    Vec<D> vec, Vec<D> min, Vec<D> max
) __MathUtil_NOEXCEPT {
    return dx::XMVectorClamp(vec.get(), min.get(), max.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV mod(
    Vec<D> vec, Vec<D> divisor
) __MathUtil_NOEXCEPT {
    return dx::XMVectorMod(vec.get(), divisor.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV pow(Vec<D> vec, Vec<D> exp) __MathUtil_NOEXCEPT {
    return dx::XMVectorPow(vec.get(), exp.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV exp(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorExp(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV log2(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorLog2(vec.get());
}

#ifdef Win10_20348
template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV log10(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorLog10(vec.get());
}
#endif

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV logE(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorLogE(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV less(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorLess(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV lessEqual(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorLessOrEqual(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV greater(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorGreater(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV greaterEqual(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorGreaterOrEqual(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV equal(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorEqual(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV equalI(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorEqualInt(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
bool MU_CALLCONV equalAll(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    if constexpr (D == 1) {
        return dx::XMVectorGetX(dx::XMVectorEqual(lhs.get(), rhs.get()));
    } else if constexpr (D == 2) {
        return dx::XMVector2Equal(lhs.get(), rhs.get());
    } else if constexpr (D == 3) {
        return dx::XMVector3Equal(lhs.get(), rhs.get());
    } else if constexpr (D == 4) {
        return dx::XMVector4Equal(lhs.get(), rhs.get());
    }
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
bool MU_CALLCONV equalAllI(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    if constexpr (D == 1) {
        return dx::XMVectorGetX(dx::XMVectorEqualInt(lhs.get(), rhs.get()));
    } else if constexpr (D == 2) {
        return dx::XMVector2EqualInt(lhs.get(), rhs.get());
    } else if constexpr (D == 3) {
        return dx::XMVector3EqualInt(lhs.get(), rhs.get());
    } else if constexpr (D == 4) {
        return dx::XMVector4EqualInt(lhs.get(), rhs.get());
    }
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV notEqual(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorNotEqual(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV notEqualI(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorNotEqualInt(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
bool MU_CALLCONV notEqualAll(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    if constexpr (D == 1) {
        return dx::XMVectorGetX(dx::XMVectorNotEqual(lhs.get(), rhs.get()));
    } else if constexpr (D == 2) {
        return dx::XMVector2NotEqual(lhs.get(), rhs.get());
    } else if constexpr (D == 3) {
        return dx::XMVector3NotEqual(lhs.get(), rhs.get());
    } else if constexpr (D == 4) {
        return dx::XMVector4NotEqual(lhs.get(), rhs.get());
    }
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
bool MU_CALLCONV notEqualAllI(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    if constexpr (D == 1) {
        return dx::XMVectorGetX(dx::XMVectorNotEqualInt(lhs.get(), rhs.get()));
    } else if constexpr (D == 2) {
        return dx::XMVector2NotEqualInt(lhs.get(), rhs.get());
    } else if constexpr (D == 3) {
        return dx::XMVector3NotEqualInt(lhs.get(), rhs.get());
    } else if constexpr (D == 4) {
        return dx::XMVector4NotEqualInt(lhs.get(), rhs.get());
    }
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV bwAnd(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorAndInt(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV bwOr(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorOrInt(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV bwXor(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorXorInt(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV bwNot(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorNearEqual(vec.get(), dx::XMVectorZero());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV lshift(Vec<D> lhs, Vec<D> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorShiftLeft(lhs.get(), rhs.get());
}

// TODO: rshift, arshift, etc.

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV sin(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorSin(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV cos(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorCos(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV tan(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorTan(vec.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV asin(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorASin(vec.get());
}

inline Radian asin(float val) __MathUtil_NOEXCEPT {
    return Radian(std::asinf(val));
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV acos(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorACos(vec.get());
}

inline Radian acos(float val) __MathUtil_NOEXCEPT {
    return Radian(std::acosf(val));
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
const Vec<D> MU_CALLCONV atan(Vec<D> vec) __MathUtil_NOEXCEPT {
    return dx::XMVectorATan(vec.get());
}

inline Radian atan(float val) __MathUtil_NOEXCEPT {
    return Radian(std::atanf(val));
}

using Vec1 = Vec<1>;
using Vec2 = Vec<2>;
using Vec3 = Vec<3>;
using Vec4 = Vec<4>;

using NVec1 = NVec<1>;
using NVec2 = NVec<2>;
using NVec3 = NVec<3>;
using NVec4 = NVec<4>;

template <std::size_t R, std::size_t C>
    requires (R >= 1 && R <= 4) && (C >= 1 && C <= 4)
class Mat;

class Quat {
public:
    friend class NQuat;

    Quat() __MathUtil_NOEXCEPT
        : Quat(dx::XMQuaternionIdentity()) {}

    Quat(float x, float y, float z, float w) __MathUtil_NOEXCEPT
        : Quat(dx::XMVectorSet(x, y, z, w)) {}

    Quat(dx::FXMVECTOR quat) __MathUtil_NOEXCEPT
        : quat_(quat) {}

    Quat(mu::Vec3 v, float w) __MathUtil_NOEXCEPT
        : quat_(v.get()) {
        quat_ = dx::XMVectorSetW(quat_, w);
    }

    Quat(class NQuat nQuat) __MathUtil_NOEXCEPT;

    Quat& MU_CALLCONV operator+=(Quat rhs) __MathUtil_NOEXCEPT {
        using dx::operator+=;
        quat_ += rhs.quat_;
        return *this;
    }

    Quat& MU_CALLCONV operator-=(Quat rhs) __MathUtil_NOEXCEPT {
        using dx::operator-=;
        quat_ -= rhs.quat_;
        return *this;
    }

    Quat& MU_CALLCONV operator*=(Quat rhs) __MathUtil_NOEXCEPT {
        quat_ = dx::XMQuaternionMultiply(quat_, rhs.quat_);
        return *this;
    }

    Quat& MU_CALLCONV operator/=(Quat rhs) __MathUtil_NOEXCEPT {
        quat_ = dx::XMQuaternionMultiply(quat_, dx::XMQuaternionInverse(rhs.quat_));
        return *this;
    }

    Quat& MU_CALLCONV operator*=(float rhs) __MathUtil_NOEXCEPT {
        quat_ = dx::XMVectorMultiply(quat_, dx::XMVectorReplicate(rhs));
        return *this;
    }

    Quat& MU_CALLCONV operator/=(float rhs) __MathUtil_NOEXCEPT {
        quat_ = dx::XMVectorDivide(quat_, dx::XMVectorReplicate(rhs));
        return *this;
    }

    dx::XMVECTOR& get() __MathUtil_NOEXCEPT {
        return quat_;
    }

    const dx::XMVECTOR& get() const __MathUtil_NOEXCEPT {
        return quat_;
    }

    const dx::XMFLOAT4 getXmf() const __MathUtil_NOEXCEPT {
        dx::XMFLOAT4 ret;
        dx::XMStoreFloat4(&ret, quat_);
        return ret;
    }

    const Quat dual() const __MathUtil_NOEXCEPT {
        return Quat(dx::XMQuaternionConjugate(quat_));
    }

    const Quat operator~() const __MathUtil_NOEXCEPT {
        return dual();
    }

    const Quat operator-() const __MathUtil_NOEXCEPT {
        return Quat(dx::XMVectorNegate(quat_));
    }

    float MU_CALLCONV x() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetX(quat_);
    }

    float MU_CALLCONV y() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetY(quat_);
    }

    float MU_CALLCONV z() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetZ(quat_);
    }

    float MU_CALLCONV w() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetW(quat_);
    }

    float MU_CALLCONV operator[](std::size_t idx) const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetByIndex(quat_, idx);
    }

    float MU_CALLCONV len() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetX(dx::XMQuaternionLength(quat_));
    }

    float MU_CALLCONV len2() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetX(dx::XMQuaternionLengthSq(quat_));
    }

    float MU_CALLCONV norm() const __MathUtil_NOEXCEPT {
        return len2();
    }

private:
    dx::XMVECTOR quat_;
};

template <std::size_t R, std::size_t C>
    requires (R >= 1 && R <= 4) && (C >= 1 && C <= 4)
class Mat;

class NQuat {
public:
    struct NoNormalize_t {};

    friend class Quat;

    NQuat() __MathUtil_NOEXCEPT
        : quat_(dx::XMQuaternionIdentity()) {}

    NQuat(float x, float y, float z, float w) __MathUtil_NOEXCEPT
        : NQuat(x, y, z, w, NoNormalize_t{}) {
        normalize();
    }

    NQuat(float x, float y, float z, float w, NoNormalize_t) __MathUtil_NOEXCEPT
        : quat_(dx::XMVectorSet(x, y, z, w)) {}

    NQuat(mu::Vec3 v, float w) __MathUtil_NOEXCEPT
        : NQuat(v, w, NoNormalize_t{}) {
        normalize();
    }

    NQuat(mu::Vec3 v, float w, NoNormalize_t) __MathUtil_NOEXCEPT
        : quat_(v.get()) {
        quat_ = dx::XMVectorSetW(quat_, w);
    }

    NQuat(Radian angle, float axisX, float axisY, float axisZ) __MathUtil_NOEXCEPT
        : quat_( dx::XMQuaternionRotationAxis(
            dx::XMVectorSet(axisX, axisY, axisZ, 0.f),
        angle) ) {}

    NQuat(Degree angle, float axisX, float axisY, float axisZ) __MathUtil_NOEXCEPT
        : NQuat( static_cast<Radian>(angle), axisX, axisY, axisZ ) {}

    NQuat(Radian angle, NVec3 axis) __MathUtil_NOEXCEPT
        : quat_( dx::XMQuaternionRotationAxis(axis.get(), angle) ) {}

    NQuat(Degree angle, NVec3 axis) __MathUtil_NOEXCEPT
        : NQuat( static_cast<Radian>(angle), axis ) {}

    NQuat(Radian roll, Radian pitch, Radian yaw) __MathUtil_NOEXCEPT
        : quat_( dx::XMQuaternionRotationRollPitchYaw(pitch, yaw, roll) ) {}

    NQuat(Degree roll, Degree pitch, Degree yaw) __MathUtil_NOEXCEPT
        : NQuat( static_cast<Radian>(roll), static_cast<Radian>(pitch),
            static_cast<Radian>(yaw)
        ) {}

    NQuat(dx::FXMVECTOR quat) __MathUtil_NOEXCEPT
        : quat_(quat) {
        normalize();
    }

    // constructor omitting normalization
    NQuat(dx::FXMVECTOR quat, NoNormalize_t) __MathUtil_NOEXCEPT
        : quat_(quat) {}

    NQuat(Quat quat) __MathUtil_NOEXCEPT
        : quat_(quat.get()) {
        normalize();
    }

    Vec3 MU_CALLCONV rotate(Vec3 vec) const __MathUtil_NOEXCEPT {
        return dx::XMVector3Rotate(vec.get(), quat_);
    }

    NQuat& MU_CALLCONV operator*=(Quat rhs) __MathUtil_NOEXCEPT {
        quat_ = dx::XMQuaternionMultiply(quat_, rhs.get());
        return *this;
    }

    NQuat& MU_CALLCONV operator*=(NQuat rhs) __MathUtil_NOEXCEPT {
        quat_ = dx::XMQuaternionMultiply(quat_, rhs.get());
        return *this;
    }

    NQuat& MU_CALLCONV operator/=(Quat rhs) __MathUtil_NOEXCEPT {
        return *this /= NQuat(rhs);
    }

    NQuat& MU_CALLCONV operator/=(NQuat rhs) __MathUtil_NOEXCEPT {
        quat_ = dx::XMQuaternionMultiply(quat_, dx::XMQuaternionInverse(rhs.get()));
        return *this;
    }

    const Mat<3, 3> MU_CALLCONV mat3() const __MathUtil_NOEXCEPT;
    const Mat<4, 4> MU_CALLCONV mat4() const __MathUtil_NOEXCEPT;

    const dx::XMMATRIX MU_CALLCONV mat() const __MathUtil_NOEXCEPT {
        return dx::XMMatrixRotationQuaternion(quat_);
    }

    dx::XMVECTOR& get() __MathUtil_NOEXCEPT {
        return quat_;
    }

    const dx::XMVECTOR& get() const __MathUtil_NOEXCEPT {
        return quat_;
    }

    const dx::XMFLOAT4 getXmf() const __MathUtil_NOEXCEPT {
        dx::XMFLOAT4 ret;
        dx::XMStoreFloat4(&ret, quat_);
        return ret;
    }

    const NQuat dual() const __MathUtil_NOEXCEPT {
        return NQuat(dx::XMQuaternionConjugate(quat_), NoNormalize_t());
    }

    const NQuat operator~() const __MathUtil_NOEXCEPT {
        return dual();
    }

    const NQuat operator-() const __MathUtil_NOEXCEPT {
        return NQuat(dx::XMVectorNegate(quat_), NoNormalize_t());
    }

    float MU_CALLCONV x() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetX(quat_);
    }

    float MU_CALLCONV y() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetY(quat_);
    }

    float MU_CALLCONV z() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetZ(quat_);
    }

    float MU_CALLCONV w() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetW(quat_);
    }

    float MU_CALLCONV operator[](std::size_t idx) const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetByIndex(quat_, idx);
    }

    float MU_CALLCONV len() const __MathUtil_NOEXCEPT {
        return 1.f;
    }

    float MU_CALLCONV len2() const __MathUtil_NOEXCEPT {
        return 1.f;
    }

    float MU_CALLCONV norm() const __MathUtil_NOEXCEPT {
        return 1.f;
    }

private:
    void normalize() __MathUtil_NOEXCEPT {
        quat_ = dx::XMQuaternionNormalize(quat_);
    }

    dx::XMVECTOR quat_;

};

inline Quat::Quat(NQuat nQuat) __MathUtil_NOEXCEPT
    : quat_(nQuat.get()) {}

inline const Quat MU_CALLCONV operator+(Quat lhs, Quat rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorAdd(lhs.get(), rhs.get());
}

inline const Quat MU_CALLCONV operator-(Quat lhs, Quat rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorSubtract(lhs.get(), rhs.get());
}

inline const Quat MU_CALLCONV operator*(Quat lhs, Quat rhs) __MathUtil_NOEXCEPT {
    return dx::XMQuaternionMultiply(lhs.get(), rhs.get());
}

inline const Quat MU_CALLCONV operator/(Quat lhs, Quat rhs) __MathUtil_NOEXCEPT {
    return dx::XMQuaternionMultiply(lhs.get(), dx::XMQuaternionInverse(rhs.get()));
}

inline const Quat MU_CALLCONV operator*(Quat lhs, float rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorMultiply(lhs.get(), dx::XMVectorReplicate(rhs));
}

inline const Quat MU_CALLCONV operator*(float lhs, Quat rhs) __MathUtil_NOEXCEPT {
    return rhs * lhs;
}

inline const Quat MU_CALLCONV operator/(Quat lhs, float rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorDivide(lhs.get(), dx::XMVectorReplicate(rhs));
}

inline const Quat MU_CALLCONV quatRPY(
    Radian roll, Radian pitch, Radian yaw
) __MathUtil_NOEXCEPT {
    return dx::XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
}

inline const Quat MU_CALLCONV quatRotAxis(
    dx::FXMVECTOR axis, Radian angle
) __MathUtil_NOEXCEPT {
    return dx::XMQuaternionRotationAxis(axis, angle);
}

inline const Quat MU_CALLCONV quatRotAxis(
    mu::Vec3 axis, Radian angle
) __MathUtil_NOEXCEPT {
    return quatRotAxis(axis.get(), angle);
}

inline const Quat MU_CALLCONV quatRotMat(
    dx::FXMMATRIX mat
) __MathUtil_NOEXCEPT {
    return dx::XMQuaternionRotationMatrix(mat);
}

inline const Quat MU_CALLCONV slerp(
    Quat lhs, Quat rhs, float t
) __MathUtil_NOEXCEPT {
    return dx::XMQuaternionSlerp(lhs.get(), rhs.get(), t);
}

inline const Quat MU_CALLCONV squad(
    Quat q0, Quat q1, Quat a, Quat b, float t
) __MathUtil_NOEXCEPT {
    return dx::XMQuaternionSquad(q0.get(), q1.get(), a.get(), b.get(), t);
}

inline float MU_CALLCONV dot(Quat lhs, Quat rhs) __MathUtil_NOEXCEPT {
    return dx::XMVectorGetX(dx::XMVector4Dot(lhs.get(), rhs.get()));
}

template <std::size_t R, std::size_t C>
    requires (R >= 1 && R <= 4) && (C >= 1 && C <= 4)
class alignas(16) Mat {
public:
    template <std::size_t R, std::size_t C>
        requires (R >= 1 && R <= 4) && (C >= 1 && C <= 4)
    friend class Mat;

    Mat() __MathUtil_NOEXCEPT
        : Mat(dx::XMMatrixIdentity()) {}
    
    Mat(float val) __MathUtil_NOEXCEPT
        : Mat(dx::XMMatrixIdentity() * val) {}

    Mat(dx::FXMMATRIX mat) __MathUtil_NOEXCEPT
        : mat_(mat) {}

    template <std::size_t R2, std::size_t C2>
    Mat(const Mat<R2, C2>& other) __MathUtil_NOEXCEPT
        : mat_(other.mat_) {}

    Mat(class NQuat nQuat) __MathUtil_NOEXCEPT
        requires ( (R == 3 && C == 3) || (R == 4 && C == 4) )
        : mat_(dx::XMMatrixRotationQuaternion(nQuat.get())) {}

    Mat& MU_CALLCONV operator+=(Mat rhs) __MathUtil_NOEXCEPT {
        using dx::operator+=;
        mat_ += rhs.mat_;
        return *this;
    }

    Mat& MU_CALLCONV operator-=(Mat rhs) __MathUtil_NOEXCEPT {
        using dx::operator-=;
        mat_ -= rhs.mat_;
        return *this;
    }

    Mat& MU_CALLCONV operator*=(Mat rhs) __MathUtil_NOEXCEPT {
        mat_ = dx::XMMatrixMultiply(mat_, rhs.mat_);
        return *this;
    }

    Mat& MU_CALLCONV operator/=(Mat rhs) __MathUtil_NOEXCEPT {
        mat_ = dx::XMMatrixMultiply(mat_, dx::XMMatrixInverse(nullptr, rhs.mat_));
        return *this;
    }
    
#ifdef _XM_NO_INTRINSICS_
    float MU_CALLCONV operator()(std::size_t r, std::size_t c) const __MathUtil_NOEXCEPT {
        return mat_(r, c)
    }
#else
    float MU_CALLCONV operator()(std::size_t r, std::size_t c) const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetByIndex(mat_.r[r], c);
    }
#endif

    const Vec<R> MU_CALLCONV row(std::size_t r) const __MathUtil_NOEXCEPT {
        return Vec<R>(mat_.r[r]);
    }

    const Vec<C> MU_CALLCONV col(std::size_t c) const __MathUtil_NOEXCEPT {
        return Vec<C>(
            dx::XMVectorGetByIndex(mat_.r[0], c),
            dx::XMVectorGetByIndex(mat_.r[1], c),
            dx::XMVectorGetByIndex(mat_.r[2], c),
            dx::XMVectorGetByIndex(mat_.r[3], c)
        );
    }

    void MU_CALLCONV setRow(std::size_t r, mu::Vec<C> vec) __MathUtil_NOEXCEPT {
        mat_.r[r] = vec.get();
    }

    float MU_CALLCONV det() const __MathUtil_NOEXCEPT {
        return dx::XMVectorGetX(dx::XMMatrixDeterminant(mat_));
    }

    dx::XMMATRIX& get() __MathUtil_NOEXCEPT {
        return mat_;
    }

    const dx::XMMATRIX& get() const __MathUtil_NOEXCEPT {
        return mat_;
    }

    const float getXmf() const __MathUtil_NOEXCEPT requires (R == 1 && C == 1) {
        return dx::XMVectorGetX(mat_.r[0]);
    }

    const dx::XMFLOAT3X3 getXmf() const __MathUtil_NOEXCEPT requires (R == 3 && C == 3) {
        dx::XMFLOAT3X3 ret;
        dx::XMStoreFloat3x3(&ret, mat_);
        return ret;
    }

    const dx::XMFLOAT4X4 getXmf() const __MathUtil_NOEXCEPT requires (R == 4 && C == 4) {
        dx::XMFLOAT4X4 ret;
        dx::XMStoreFloat4x4(&ret, mat_);
        return ret;
    }

    const Mat MU_CALLCONV operator-() const __MathUtil_NOEXCEPT {
        return dx::XMMatrixMultiply( mat_, dx::XMMatrixScalingFromVector(
            dx::XMVectorNegate( dx::XMVectorSplatOne() )
        ) );
    }

private:
    dx::XMMATRIX mat_;
};

template <std::size_t R, std::size_t C>
const Mat<R, C> MU_CALLCONV operator+(Mat<R, C> lhs, Mat<R, C> rhs) __MathUtil_NOEXCEPT {
    return lhs.get() + rhs.get();
}

template <std::size_t R, std::size_t C>
const Mat<R, C> MU_CALLCONV operator-(Mat<R, C> lhs, Mat<R, C> rhs) __MathUtil_NOEXCEPT {
    return lhs.get() - rhs.get();
}

template <std::size_t R, std::size_t C>
const Mat<R, C> MU_CALLCONV operator*(Mat<R, C> lhs, Mat<R, C> rhs) __MathUtil_NOEXCEPT {
    return dx::XMMatrixMultiply(lhs.get(), rhs.get());
}

template <std::size_t R, std::size_t C>
const Mat<R, C> MU_CALLCONV operator/(Mat<R, C> lhs, Mat<R, C> rhs) __MathUtil_NOEXCEPT {
    return dx::XMMatrixMultiply(lhs.get(), dx::XMMatrixInverse(nullptr, rhs.get()));
}

template <std::size_t R, std::size_t C>
const Vec<C> MU_CALLCONV operator*(Vec<R> lhs, Mat<R, C> rhs) __MathUtil_NOEXCEPT {
    return dx::XMVector4Transform(lhs.get(), rhs.get());
}

template <std::size_t D>
    requires (D >= 1 && D <= 4)
Vec<D>& MU_CALLCONV Vec<D>::operator*=(Mat<D, D> rhs) __MathUtil_NOEXCEPT {
    vec_ = dx::XMVector4Transform(vec_, rhs.get());
    return *this;
}

inline const Mat<3, 3> MU_CALLCONV scale(float xScl, float yScl, float zScl) __MathUtil_NOEXCEPT {
    return dx::XMMatrixScaling(xScl, yScl, zScl);
}

inline const Mat<3, 3> MU_CALLCONV scale(Vec<3> vec) __MathUtil_NOEXCEPT {
    return dx::XMMatrixScalingFromVector(vec.get());
}

inline const Mat<4, 4> MU_CALLCONV scaleH(Vec<3> vec) __MathUtil_NOEXCEPT {
    return dx::XMMatrixScalingFromVector(vec.get());
}

inline const Mat<4, 4> MU_CALLCONV translate(float x, float y, float z) __MathUtil_NOEXCEPT {
    return dx::XMMatrixTranslation(x, y, z);
}

inline const Mat<4, 4> MU_CALLCONV translate(Vec<3> vec) __MathUtil_NOEXCEPT {
    return dx::XMMatrixTranslationFromVector(vec.get());
}

inline const Mat<3, 3> MU_CALLCONV rotate(Radian angle, float axisX, float axisY, float axisZ) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationAxis(dx::XMVectorSet(axisX, axisY, axisZ, 0.f), angle);
}

inline const Mat<3, 3> MU_CALLCONV rotate(Radian angle, Vec<3> axis) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationAxis(axis.get(), angle);
}

inline const Mat<3, 3> MU_CALLCONV rotate(Radian angle, NVec<3> axis) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationNormal(axis.get(), angle);
}

inline const Mat<3, 3> MU_CALLCONV rotate(Degree angle, float axisX, float axisY, float axisZ) __MathUtil_NOEXCEPT {
    return rotate(static_cast<Radian>(angle), axisX, axisY, axisZ);
}

inline const Mat<3, 3> MU_CALLCONV rotate(Degree angle, Vec<3> axis) __MathUtil_NOEXCEPT {
    return rotate(static_cast<Radian>(angle), axis);
}

inline const Mat<3, 3> MU_CALLCONV rotate(Degree angle, NVec<3> axis) __MathUtil_NOEXCEPT {
    return rotate(static_cast<Radian>(angle), axis);
}

inline const Mat<3, 3> MU_CALLCONV rotateX(Radian angle) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationX(angle);
}

inline const Mat<3, 3> MU_CALLCONV rotateX(Degree angle) __MathUtil_NOEXCEPT {
    return rotateX(static_cast<Radian>(angle));
}

inline const Mat<3, 3> MU_CALLCONV rotateY(Radian angle) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationY(angle);
}

inline const Mat<3, 3> MU_CALLCONV rotateY(Degree angle) __MathUtil_NOEXCEPT {
    return rotateY(static_cast<Radian>(angle));
}

inline const Mat<3, 3> MU_CALLCONV rotateZ(Radian angle) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationZ(angle);
}

inline const Mat<3, 3> MU_CALLCONV rotateZ(Degree angle) __MathUtil_NOEXCEPT {
    return rotateZ(static_cast<Radian>(angle));
}

inline const Mat<3, 3> MU_CALLCONV rotateRPY(Radian roll, Radian pitch, Radian yaw) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationRollPitchYaw(pitch, yaw, roll);
}

inline const Mat<3, 3> MU_CALLCONV rotateRPY(Degree roll, Degree pitch, Degree yaw) __MathUtil_NOEXCEPT {
    return rotateRPY(
        static_cast<Radian>(roll),
        static_cast<Radian>(pitch),
        static_cast<Radian>(yaw)
    );
}

inline const Mat<4, 4> MU_CALLCONV rotateH(Radian angle, float axisX, float axisY, float axisZ) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationAxis(dx::XMVectorSet(axisX, axisY, axisZ, 0.f), angle);
}

inline const Mat<4, 4> MU_CALLCONV rotateH(Radian angle, Vec<3> axis) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationAxis(axis.get(), angle);
}

inline const Mat<4, 4> MU_CALLCONV rotateH(Radian angle, NVec<3> axis) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationNormal(axis.get(), angle);
}

inline const Mat<4, 4> MU_CALLCONV rotateH(Degree angle, float axisX, float axisY, float axisZ) __MathUtil_NOEXCEPT {
    return rotateH(static_cast<Radian>(angle), axisX, axisY, axisZ);
}

inline const Mat<4, 4> MU_CALLCONV rotateH(Degree angle, Vec<3> axis) __MathUtil_NOEXCEPT {
    return rotateH(static_cast<Radian>(angle), axis);
}

inline const Mat<4, 4> MU_CALLCONV rotateH(Degree angle, NVec<3> axis) __MathUtil_NOEXCEPT {
    return rotateH(static_cast<Radian>(angle), axis);
}

inline const Mat<4, 4> MU_CALLCONV rotateXH(Radian angle) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationX(angle);
}

inline const Mat<4, 4> MU_CALLCONV rotateXH(Degree angle) __MathUtil_NOEXCEPT {
    return rotateXH(static_cast<Radian>(angle));
}

inline const Mat<4, 4> MU_CALLCONV rotateYH(Radian angle) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationY(angle);
}

inline const Mat<4, 4> MU_CALLCONV rotateYH(Degree angle) __MathUtil_NOEXCEPT {
    return rotateYH(static_cast<Radian>(angle));
}

inline const Mat<4, 4> MU_CALLCONV rotateZH(Radian angle) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationZ(angle);
}

inline const Mat<4, 4> MU_CALLCONV rotateZH(Degree angle) __MathUtil_NOEXCEPT {
    return rotateZH(static_cast<Radian>(angle));
}

inline const Mat<4, 4> MU_CALLCONV rotateRPYH(Radian roll, Radian pitch, Radian yaw) __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationRollPitchYaw(pitch, yaw, roll);
}

inline const Quat MU_CALLCONV quatRotMat(
    mu::Mat<3, 3> mat
) __MathUtil_NOEXCEPT {
    return quatRotMat(mat.get());
}

inline const Quat MU_CALLCONV quatRotMat(
    mu::Mat<4, 4> mat
) __MathUtil_NOEXCEPT {
    return quatRotMat(mat.get());
}

inline const NQuat MU_CALLCONV quatFromTo(
    NVec<3> nFrom, NVec<3> nTo
) __MathUtil_NOEXCEPT {
    const auto d = dot(nFrom, nTo);
    if (d >= 1.f) {
        return NQuat();
    } else if (d <= -1.f) {
        // use axis perpendicular to from
        auto axis = cross(nFrom, NVec<3>(1.f, 0.f, 0.f, NVec<3>::NoNormalize_t{}));
        if (axis.len2() < 0.0001f) {
            axis = cross(nFrom, NVec<3>(0.f, 1.f, 0.f, NVec<3>::NoNormalize_t{}));
        }

        return quatRotAxis(axis, Radian(pi));
    }

    const auto axis = cross(nFrom, nTo);
    const auto angle = acos(d);
    return quatRotAxis(axis, angle);
}

inline const Mat<3, 3> MU_CALLCONV transpose(Mat<3, 3> mat) __MathUtil_NOEXCEPT {
    return dx::XMMatrixTranspose(mat.get());
}

inline const Mat<4, 4> MU_CALLCONV transpose(Mat<4, 4> mat) __MathUtil_NOEXCEPT {
    return dx::XMMatrixTranspose(mat.get());
}

inline const Mat<3, 3> MU_CALLCONV inverse(Mat<3, 3> mat) __MathUtil_NOEXCEPT {
    return dx::XMMatrixInverse(nullptr, mat.get());
}

inline const Mat<3, 3> MU_CALLCONV inverse(Mat<3, 3> mat, float& outDet) __MathUtil_NOEXCEPT {
    dx::XMVECTOR det;
    auto ret = dx::XMMatrixInverse(&det, mat.get());
    outDet = dx::XMVectorGetX(det);
    return ret;
}

inline const Mat<4, 4> MU_CALLCONV inverse(Mat<4, 4> mat) __MathUtil_NOEXCEPT {
    return dx::XMMatrixInverse(nullptr, mat.get());
}

inline const Mat<4, 4> MU_CALLCONV inverse(Mat<4, 4> mat, float& outDet) __MathUtil_NOEXCEPT {
    dx::XMVECTOR det;
    auto ret = dx::XMMatrixInverse(&det, mat.get());
    outDet = dx::XMVectorGetX(det);
    return ret;
}

inline const Mat<4, 4> MU_CALLCONV lookAt(
    Vec<3> eye, Vec<3> at, NVec<3> up
) __MathUtil_NOEXCEPT {
    return dx::XMMatrixLookAtLH(eye.get(), at.get(), up.get());
}

inline const Mat<4, 4> MU_CALLCONV ortho(
    float width, float height, float nearZ, float farZ
) __MathUtil_NOEXCEPT {
    return dx::XMMatrixOrthographicLH(width, height, nearZ, farZ);
}

inline const Mat<4, 4> MU_CALLCONV ortho(
    float left, float right, float bottom, float top, float nearZ, float farZ
) __MathUtil_NOEXCEPT {
    return dx::XMMatrixOrthographicOffCenterLH(left, right, bottom, top, nearZ, farZ);
}

inline const Mat<4, 4> MU_CALLCONV persp(
    float width, float height, float nearZ, float farZ
) __MathUtil_NOEXCEPT {
    return dx::XMMatrixPerspectiveLH(width, height, nearZ, farZ);
}

inline const Mat<4, 4> MU_CALLCONV persp(
    Radian fov, float aspect, float nearZ, float farZ
) __MathUtil_NOEXCEPT {
    return dx::XMMatrixPerspectiveFovLH(fov, aspect, nearZ, farZ);
}

inline const Mat<4, 4> MU_CALLCONV persp(
    Degree fov, float aspect, float nearZ, float farZ
) __MathUtil_NOEXCEPT {
    return persp(static_cast<Radian>(fov), aspect, nearZ, farZ);
}

inline const Mat<4, 4> MU_CALLCONV persp(
    float left, float right, float bottom, float top, float nearZ, float farZ
) __MathUtil_NOEXCEPT {
    return dx::XMMatrixPerspectiveOffCenterLH(left, right, bottom, top, nearZ, farZ);
}

inline const Mat<3, 3> MU_CALLCONV NQuat::mat3() const __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationQuaternion(quat_);
}

inline const Mat<4, 4> MU_CALLCONV NQuat::mat4() const __MathUtil_NOEXCEPT {
    return dx::XMMatrixRotationQuaternion(quat_);
}

using Mat1x1 = Mat<1, 1>;
using Mat1x2 = Mat<1, 2>;
using Mat1x3 = Mat<1, 3>;
using Mat1x4 = Mat<1, 4>;
using Mat2x1 = Mat<2, 1>;
using Mat2x2 = Mat<2, 2>;
using Mat2x3 = Mat<2, 3>;
using Mat2x4 = Mat<2, 4>;
using Mat3x1 = Mat<3, 1>;
using Mat3x2 = Mat<3, 2>;
using Mat3x3 = Mat<3, 3>;
using Mat3x4 = Mat<3, 4>;
using Mat4x1 = Mat<4, 1>;
using Mat4x2 = Mat<4, 2>;
using Mat4x3 = Mat<4, 3>;
using Mat4x4 = Mat<4, 4>;

}   // namespace mu

#endif  // __MathUtil_HPP