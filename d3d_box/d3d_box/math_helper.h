#pragma once

#include <Windows.h>
#include <DirectXMath.h>
#include <cstdint>
#include <cstdlib>
#include <concepts>

namespace math_helper
{
	inline float randf()
	{
		return static_cast<float>(std::rand()) / RAND_MAX;
	}

	inline float randf(float min, float max)
	{
		return min + randf() * max;
	}

	template<std::integral T>
	inline T rand(T min, T max)
	{
		return min + std::rand() % (max - min + static_cast<T>(1));
	}

	template<typename T>
		requires requires(T a, T b, float t) { {a - b} -> std::same_as<T>; a * t; }
	inline constexpr T lerp(T const& a, T const& b, float t)
	{
		return a + (b - a) * t;
	}

	template<typename T>
		requires requires(T a, T b) { a < b; a > b; }
	inline constexpr T clamp(T const& x, T const& min, T const& max)
	{
		return x < min ? min : x > max ? max : x;
	}

	inline constexpr DirectX::XMFLOAT4X4 identity4x4 = DirectX::XMFLOAT4X4
	{
		1.0f, 0, 0, 0,
		0, 1.0f, 0, 0,
		0, 0, 1.0f, 0,
		0, 0, 0, 1.0f
	};

	inline constexpr float infinity = FLT_MAX;
	inline constexpr float pi = 3.1415926535f;
}